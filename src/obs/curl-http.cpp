#include "obs/curl-http.hpp"

#include <curl/curl.h>

#include <QFile>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include <cctype>
#include <utility>

namespace langlive::obsplugin::obs {

namespace {

struct Transfer {
	std::string body;
	QFile *file = nullptr;
	size_t maxBytes = 0;
	bool tooLarge = false;
	bool writeFailed = false;

	const std::atomic<bool> *stopping = nullptr;
	const std::atomic<bool> *cancel = nullptr; // optional
	std::function<void(int)> reportProgress;   // called on the worker thread
	int lastPercent = -1;
};

size_t OnData(char *data, size_t size, size_t count, void *user)
{
	auto *transfer = static_cast<Transfer *>(user);
	const size_t bytes = size * count;
	if (transfer->file) {
		if (transfer->file->write(data, static_cast<qint64>(bytes)) != static_cast<qint64>(bytes)) {
			transfer->writeFailed = true;
			return 0;
		}
		return bytes;
	}
	if (transfer->body.size() + bytes > transfer->maxBytes) {
		transfer->tooLarge = true;
		return 0; // aborts the transfer
	}
	transfer->body.append(data, bytes);
	return bytes;
}

int OnProgress(void *user, curl_off_t downloadTotal, curl_off_t downloaded, curl_off_t, curl_off_t)
{
	auto *transfer = static_cast<Transfer *>(user);
	if (transfer->stopping->load() || (transfer->cancel && transfer->cancel->load()))
		return 1; // aborts the transfer
	if (transfer->reportProgress && downloadTotal > 0) {
		const int percent = static_cast<int>(downloaded * 100 / downloadTotal);
		if (percent != transfer->lastPercent) {
			transfer->lastPercent = percent;
			transfer->reportProgress(percent);
		}
	}
	return 0;
}

bool IsHttps(const std::string &url)
{
	static const std::string scheme = "https://";
	if (url.size() < scheme.size())
		return false;
	for (size_t i = 0; i < scheme.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(url[i])) != scheme[i])
			return false;
	return true;
}

HttpResponse Perform(const HttpRequest &request, Transfer &transfer)
{
	HttpResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	curl_slist *headers = nullptr;
	for (const std::string &header : request.headers)
		headers = curl_slist_append(headers, header.c_str());
	// No "Expect: 100-continue" round trip before a body.
	headers = curl_slist_append(headers, "Expect:");

	curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // required on worker threads
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, request.stallTimeoutSeconds);
	if (request.totalTimeoutSeconds > 0) {
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.totalTimeoutSeconds);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, request.totalTimeoutSeconds);
	}
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
	if (request.followRedirects) {
		// Never follow a redirect from HTTPS to HTTP (HTTP is only for local
		// test servers). The option needs libcurl 7.85; macOS's own libcurl
		// is older on macOS 12, where the limit cannot be set: no redirects
		// at all then, rather than any.
		const CURLcode limited = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR,
							  IsHttps(request.url) ? "https" : "http,https");
		if (limited == CURLE_OK) {
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
		}
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	if (request.method == "GET") {
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	} else {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
		if (request.method != "POST")
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
	}

	const CURLcode result = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	if (transfer.tooLarge)
		response.error = "response too large";
	else if (transfer.writeFailed)
		response.error = "cannot write the downloaded file";
	else if (transfer.stopping->load() || (transfer.cancel && transfer.cancel->load()))
		response.error = "canceled";
	else if (result != CURLE_OK)
		response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	response.body = std::move(transfer.body);
	return response;
}

} // namespace

CurlHttp::CurlHttp() : context_(std::make_unique<QObject>()), stopping_(std::make_shared<std::atomic<bool>>(false))
{
	// Reference counted by libcurl; OBS has initialized it already.
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlHttp::~CurlHttp()
{
	stopping_->store(true);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (Worker &worker : workers_)
			if (worker.thread.joinable())
				worker.thread.join();
		workers_.clear();
	}
	// Drops callbacks that were queued but not delivered.
	context_.reset();
	curl_global_cleanup();
}

HttpResponse CurlHttp::SendNow(const HttpRequest &request)
{
	Transfer transfer;
	transfer.maxBytes = request.maxResponseBytes;
	transfer.stopping = stopping_.get();
	return Perform(request, transfer);
}

void CurlHttp::Reap()
{
	for (auto it = workers_.begin(); it != workers_.end();) {
		if (it->finished.load()) {
			it->thread.join();
			it = workers_.erase(it);
		} else {
			++it;
		}
	}
}

void CurlHttp::Send(HttpRequest request, std::function<void(HttpResponse)> done, std::function<void(int)> progress)
{
	std::lock_guard<std::mutex> lock(mutex_);
	Reap();
	Worker &worker = workers_.emplace_back();

	QObject *context = context_.get();
	std::shared_ptr<std::atomic<bool>> stopping = stopping_;
	worker.thread =
		std::thread([request = std::move(request), done = std::move(done), progress = std::move(progress),
			     context, stopping, finished = &worker.finished]() mutable {
			Transfer transfer;
			transfer.maxBytes = request.maxResponseBytes;
			transfer.stopping = stopping.get();
			transfer.cancel = request.cancel.get();
			if (progress)
				transfer.reportProgress = [context, stopping, progress](int percent) {
					QMetaObject::invokeMethod(
						context,
						[stopping, progress, percent]() {
							if (!stopping->load())
								progress(percent);
						},
						Qt::QueuedConnection);
				};

			HttpResponse response;
			QFile file;
			if (!request.outputPath.empty()) {
				file.setFileName(QString::fromStdString(request.outputPath));
				if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
					transfer.file = &file;
				else
					response.error = "cannot write " + request.outputPath;
			}
			if (response.error.empty())
				response = Perform(request, transfer);
			if (file.isOpen())
				file.close();

			QMetaObject::invokeMethod(
				context,
				[stopping, done = std::move(done), response = std::move(response)]() mutable {
					if (!stopping->load())
						done(std::move(response));
				},
				Qt::QueuedConnection);
			finished->store(true);
		});
}

} // namespace langlive::obsplugin::obs
