/*
LangLive OBS Plugin
Copyright (C) 2026 LangLive

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "app/plugin-controller.hpp"
#include "obs/http-langlive-api.hpp"
#include "obs/obs-settings-store.hpp"
#include "obs/obs-streaming-host.hpp"
#include "obs/qa-override.hpp"
#include "obs/reconnect-watcher.hpp"
#include "obs/start-failure-watcher.hpp"
#include "obs/update-feed.hpp"
#include "ui/langlive-menu.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

using langlive::obsplugin::PluginController;
using langlive::obsplugin::obs::HttpLangLiveApi;
using langlive::obsplugin::obs::ObsSettingsStore;
using langlive::obsplugin::obs::ObsStreamingHost;
using langlive::obsplugin::obs::QtUpdateFeed;
using langlive::obsplugin::obs::ReconnectWatcher;
using langlive::obsplugin::obs::StartFailureWatcher;
using langlive::obsplugin::ui::LangLiveMenu;

// QA builds read qa-override.json from the plugin's config folder; production
// builds have no override at all.
std::string QaOverridePath()
{
#if LANGLIVE_QA_BUILD
	char *path = obs_module_config_path(langlive::obsplugin::obs::kQaOverrideFileName);
	std::string result = path ? path : "";
	bfree(path);
	return result;
#else
	return {};
#endif
}

struct Plugin {
	ObsStreamingHost host;
	ObsSettingsStore store;
	PluginController controller{host, store};
	// After the controller: destroyed first, so a reply finishing during
	// shutdown never calls into a destroyed controller.
	QtUpdateFeed updateFeed{LANGLIVE_UPDATE_MANIFEST_URL, std::string("langlive-obs-plugin/") + PLUGIN_VERSION,
				QaOverridePath()};
	// Unset (the controller's mock backend) when the build has no API URL.
	std::unique_ptr<HttpLangLiveApi> api;
	ReconnectWatcher reconnectWatcher{[this](bool reconnecting) {
		if (reconnecting)
			controller.OnStreamReconnecting();
		else
			controller.OnStreamReconnected();
	}};
	// Created with the main window; destroyed before the controller.
	std::unique_ptr<StartFailureWatcher> startFailureWatcher;
	QPointer<LangLiveMenu> menu;
};

std::unique_ptr<Plugin> g_plugin;

// OBS has no pre-start veto hook: by the time STREAMING_STARTED fires the
// connection is up. Failing streams are stopped right away; the dialog is
// shown after the callback returns so it does not block OBS's event
// dispatch.
void OnStreamingStarted()
{
	auto violations = g_plugin->controller.OnStreamingStarted();
	if (violations.empty() || !g_plugin->menu)
		return;

	QMetaObject::invokeMethod(
		g_plugin->menu,
		[menu = g_plugin->menu, violations = std::move(violations)]() mutable {
			if (menu)
				menu->ShowPreflightFailure(std::move(violations));
		},
		Qt::QueuedConnection);
}

void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	if (!g_plugin)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		g_plugin->controller.OnStreamingStarting();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		g_plugin->reconnectWatcher.Start();
		OnStreamingStarted();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		// No update check here: the menu checks when the streamer uses
		// it (see LangLiveMenu::CheckThen).
		g_plugin->controller.CheckSavedLogin();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// Before the plugin unloads, which ends requests still in flight.
		g_plugin->controller.OnExit();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		g_plugin->reconnectWatcher.Stop();
		if (g_plugin->controller.OnStreamingStopped())
			obs_log(LOG_INFO, "live ended");
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded (version %s%s)", PLUGIN_VERSION, LANGLIVE_QA_BUILD ? ", QA build" : "");
	return true;
}

MODULE_EXPORT void obs_module_post_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_WARNING, "no OBS main window; plugin UI disabled");
		return;
	}

	g_plugin = std::make_unique<Plugin>();
	g_plugin->controller.SetUpdateFeed(&g_plugin->updateFeed, PLUGIN_VERSION);
	g_plugin->controller.SetWebsite(LANGLIVE_WEBSITE_URL, obs_get_locale());
	g_plugin->controller.SetLogCallback([](PluginController::LogLevel level, const std::string &message) {
		obs_log(level == PluginController::LogLevel::Warning ? LOG_WARNING : LOG_INFO, "%s", message.c_str());
	});
	if (std::string(LANGLIVE_API_BASE_URL).empty()) {
		// Development and CI builds without the variable; the CI release job
		// refuses to publish such a build.
		obs_log(LOG_WARNING,
			"no LangLive API URL in this build: using the mock backend (login always succeeds)");
	} else {
		const std::string apiLocale = langlive::obsplugin::ApiLocale(obs_get_locale());
		obs_log(LOG_INFO, "LangLive API language: %s (OBS: %s)", apiLocale.c_str(), obs_get_locale());
		g_plugin->api = std::make_unique<HttpLangLiveApi>(
			LANGLIVE_API_BASE_URL, std::string("langlive-obs-plugin/") + PLUGIN_VERSION, apiLocale);
		g_plugin->controller.SetApi(g_plugin->api.get());
	}
	g_plugin->menu = new LangLiveMenu(g_plugin->controller, mainWindow, LANGLIVE_QA_BUILD);
	g_plugin->controller.SetStateChangedCallback([]() {
		if (g_plugin && g_plugin->menu)
			g_plugin->menu->Refresh();
	});
	// Called in the middle of the controller's handling of a reply: the
	// notice is modal, so show it after that returns.
	g_plugin->controller.SetLoginLostCallback([](langlive::obsplugin::ApiError::Kind reason) {
		if (!g_plugin || !g_plugin->menu)
			return;
		QMetaObject::invokeMethod(
			g_plugin->menu,
			[menu = g_plugin->menu, reason]() {
				if (menu)
					menu->ShowLoginLost(reason);
			},
			Qt::QueuedConnection);
	});
	g_plugin->startFailureWatcher =
		std::make_unique<StartFailureWatcher>(mainWindow, []() { g_plugin->controller.OnStreamStartFailed(); });
	if (!g_plugin->startFailureWatcher->IsWatching())
		obs_log(LOG_WARNING, "cannot watch for failed stream starts in this OBS version");
	// From OBS's STREAMING_STOPPED callback: show the notice after it returns.
	g_plugin->controller.SetLiveEndedOutsideCallback([]() {
		if (!g_plugin || !g_plugin->menu)
			return;
		QMetaObject::invokeMethod(
			g_plugin->menu,
			[menu = g_plugin->menu]() {
				if (menu)
					menu->ShowLiveEndedOutside();
			},
			Qt::QueuedConnection);
	});

	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	if (g_plugin) {
		g_plugin->controller.SetStateChangedCallback(nullptr);
		g_plugin->controller.SetLoginLostCallback(nullptr);
		g_plugin->controller.SetLiveEndedOutsideCallback(nullptr);
		g_plugin->controller.SetApi(nullptr);
		g_plugin->startFailureWatcher.reset();
		// The menu references the controller; it must not outlive it.
		delete g_plugin->menu.data();
	}
	g_plugin.reset();
	obs_log(LOG_INFO, "plugin unloaded");
}
