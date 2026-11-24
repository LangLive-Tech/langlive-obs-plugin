#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class QObject;

namespace langlive::obsplugin::obs {

struct HttpRequest {
	std::string method = "GET"; // GET, POST or PUT
	std::string url;
	std::vector<std::string> headers; // "Name: value"
	std::string body;                 // sent for POST and PUT
	// When set, the response body is written to this file instead of
	// HttpResponse::body, and maxResponseBytes does not apply.
	std::string outputPath;
	size_t maxResponseBytes = 1024 * 1024;
	long stallTimeoutSeconds = 15; // abort when no data arrives for this long
	long totalTimeoutSeconds = 0;  // abort after this long in all; 0: no limit
	// Off for the LangLive API: libcurl drops only its standard credential
	// headers on a redirect to another host, so X-Authorization-Token and
	// the body (phone number, code) would follow. The update manifest and
	// installers may be redirected (CDN), within HTTPS.
	bool followRedirects = true;
	// Set to true from any thread to abort the transfer ("canceled").
	std::shared_ptr<std::atomic<bool>> cancel;
};

struct HttpResponse {
	long status = 0;   // HTTP status; 0 if no response arrived
	std::string body;  // empty when written to outputPath
	std::string error; // transport error; empty if a response arrived

	bool ok() const { return error.empty(); }
};

// HTTP(S) with libcurl, which OBS itself uses. Qt Network cannot be used: OBS
// does not ship Qt's TLS backends, so every HTTPS request fails with "TLS
// initialization failed".
//
// Each request runs on its own thread; `progress` and `done` run on the main
// thread. Create and destroy on the main thread. Destroying the client
// aborts requests in flight and waits for their threads, and their callbacks
// are not called.
class CurlHttp {
public:
	CurlHttp();
	~CurlHttp();
	CurlHttp(const CurlHttp &) = delete;
	CurlHttp &operator=(const CurlHttp &) = delete;

	void Send(HttpRequest request, std::function<void(HttpResponse)> done,
		  std::function<void(int percent)> progress = {});

	// Sends on the calling thread and waits for the answer. Only for when
	// OBS is exiting and no callback could run any more; set
	// totalTimeoutSeconds so OBS is not held up.
	HttpResponse SendNow(const HttpRequest &request);

private:
	struct Worker {
		std::thread thread;
		std::atomic<bool> finished{false};
	};

	// Joins the threads of finished requests.
	void Reap();

	// Receives the callbacks queued from worker threads. Deleting it drops
	// the ones not delivered yet, while the plugin is still loaded.
	std::unique_ptr<QObject> context_;
	std::shared_ptr<std::atomic<bool>> stopping_;
	std::mutex mutex_;
	std::list<Worker> workers_;
};

} // namespace langlive::obsplugin::obs
