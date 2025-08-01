#include "hex/plugin.hpp"
#include "discord.h"

#include "hex/api/content_registry.hpp"
#include "hex/api/event_manager.hpp"
#include "hex/api/events/events_gui.hpp"
#include "hex/api/events/events_provider.hpp"
#include "hex/api/imhex_api.hpp"
#include "hex/api/localization_manager.hpp"
#include "hex/api/project_file_manager.hpp"
#include "hex/helpers/fmt.hpp"
#include "hex/helpers/logger.hpp"
#include "hex/providers/provider.hpp"
#include "hex/ui/view.hpp"

#include "romfs/romfs.hpp"

namespace ImHexSettings = hex::ContentRegistry::Settings;
namespace ImHexWidgets = ImHexSettings::Widgets;

//! An enum to describe the state that the user is in for ImHex.
enum UserStatus : int
{
    NONE,
    EDITING_PROVIDER,
    VIEWING_PROVIDER,
    VIEWING_ACHIEVEMENTS,
    VIEWING_CONTENT_STORE,
    VIEWING_THEME_MANAGER,
    VIEWING_SETTINGS,
    VIEWING_ABOUT,
    MAX,
};

namespace rpcdata
{
    constexpr static auto clientId = 1400779994891944026u;
    constexpr static auto largeImage = "icon_1024";
} // namespace rpcdata

namespace views
{
    constexpr auto ABOUT = "hex.builtin.view.about.name";
    constexpr auto ACHIEVEMENTS = "hex.builtin.view.achievements.name";
    constexpr auto CONSTANTS = "hex.builtin.view.constants.name";
    constexpr auto CONTENT_STORE = "hex.builtin.view.store.name";
    constexpr auto DATA_PROCESSOR = "hex.builtin.view.data_processor.name";
    constexpr auto HELP = "hex.builtin.view.help.name";
    constexpr auto HEX_EDITOR = "hex.builtin.view.hex_editor.name";
    constexpr auto LOG_CONSOLE = "hex.builtin.view.log_console.name";
    constexpr auto PATCHES = "hex.builtin.view.patches.name";
    constexpr auto PATTERN_DATA = "hex.builtin.view.pattern_data.name";
    constexpr auto PATTERN_EDITOR = "hex.builtin.view.pattern_editor.name";
    constexpr auto PROVIDER_SETTINGS = "hex.builtin.view.provider_settings.name";
    constexpr auto SETTINGS = "hex.builtin.view.settings.name";
    constexpr auto THEME_MANAGER = "hex.builtin.view.theme_manager.name";
    constexpr auto TUTORIAL = "hex.builtin.view.tutorials.name";
} // namespace views

namespace lang
{
    constexpr const char *status[UserStatus::MAX] = {
        "hex.ImHexDiscordRPC.settings.none",
        "hex.ImHexDiscordRPC.settings.editingProvider",
        "hex.ImHexDiscordRPC.settings.viewingProvider",
        "hex.ImHexDiscordRPC.settings.viewingAchievements",
        "hex.ImHexDiscordRPC.settings.viewingContentStore",
        "hex.ImHexDiscordRPC.settings.viewingThemeManager",
        "hex.ImHexDiscordRPC.settings.viewingSettings",
        "hex.ImHexDiscordRPC.settings.viewingAbout",
    };

    // clang-format off
    constexpr auto category        = "hex.ImHexDiscordRPC.settings";
    constexpr auto description     = "hex.ImHexDiscordRPC.settings.description";
    constexpr auto enabled         = "hex.ImHexDiscordRPC.settings.enabled";
    constexpr auto showProject     = "hex.ImHexDiscordRPC.settings.showProject";
    constexpr auto showProvider    = "hex.ImHexDiscordRPC.settings.showProvider";
    constexpr auto showStatus      = "hex.ImHexDiscordRPC.settings.showStatus";
    constexpr auto showTimestamp   = "hex.ImHexDiscordRPC.settings.showTimestamp";
    constexpr auto useRelativeTime = "hex.ImHexDiscordRPC.settings.useRelativeTime";
    // clang-format on
} // namespace lang

namespace settings
{
    static bool enabled = false;
    static bool showProject = false;
    static bool showProvider = false;
    static bool showStatus = false;
    static bool showTimestamp = false;
    static bool useRelativeTime = false;

    bool isEnabled()
    {
        return enabled;
    }
} // namespace settings

static UserStatus userStatus = NONE;

std::unique_ptr<discord::Core *> discordCore = nullptr;
std::unique_ptr<discord::Activity> discordActivity = nullptr;
static time_t startTimestamp;
static bool updateTimestamp = true;

//! A helper method for getting the global (main) Discord activity.
//! Mainly used because it prints an error when necessary.
//! @return
discord::Activity *getActivity()
{
    if (discordActivity == nullptr) {
        hex::log::error("Discord activity shouldn't be nullptr!");
        return nullptr;
    }

    return discordActivity.get();
}

//! A helper method for getting the Discord core.
//! Mainly used because it prints an error when necessary.
//! @return
discord::Core *getCore()
{
    if (discordCore == nullptr || *discordCore == nullptr) {
        hex::log::warn("Discord core shouldn't be nullptr! Rerunning initialisation.");
        return nullptr;
    }

    return *discordCore.get();
}

//! Sets the state for a given Discord activity.
//! @param activity The activity
void setActivityState(discord::Activity *activity)
{
    std::string details{};
    std::string state{};

    const auto hasProject = settings::showProject && hex::ProjectFile::hasPath();
    const auto hasProvider = settings::showProvider &&
                             hex::ImHexApi::Provider::isValid() &&
                             hex::ImHexApi::Provider::get() != nullptr;

    // Set the details, either project name or provider name.
    if (hasProject) {
        details = hex::ProjectFile::getPath().filename().stem().string();
    } else if (hasProvider) {
        details = hex::ImHexApi::Provider::get()->getName();
    }

    // Set the state text, which can be either the provider, status text or none.
    if (hasProject && hasProvider) {
        state = hex::ImHexApi::Provider::get()->getName();
    } else if (userStatus != NONE) {
        state = hex::LocalizationManager::getLocalizedString(lang::status[userStatus]);
    }

    // If the details is empty, it means we don't have a project or provider loaded.
    // Use state text as details in this case.
    if (details.empty()) {
        details = state;
        state.clear();
    }

    activity->SetDetails(details.c_str());
    activity->SetState(state.c_str());
}

void setActivityTimestamp(discord::Activity *activity)
{
    if (settings::showTimestamp) {
        if (updateTimestamp) {
            startTimestamp = std::time(nullptr);
            updateTimestamp = false;
        }

        activity->GetTimestamps().SetStart(startTimestamp);
    } else {
        activity->GetTimestamps().SetStart(0);
        updateTimestamp = true;
    }
}

void updateActivity()
{
    const auto core = getCore();

    if (!settings::enabled) {
        core->ActivityManager().UpdateActivity({}, [](auto) {});
        return;
    }

    const auto activity = getActivity();
    setActivityTimestamp(activity);
    setActivityState(activity);

    core->ActivityManager().UpdateActivity(*activity, [](const discord::Result res) {
        if (res == discord::Result::Ok) {
            hex::log::info("Discord activity updated!");
        } else {
            hex::log::error("Failed to update Discord activity. :c");
        }
    });
}

void updateStatus(const UserStatus status)
{
    userStatus = status;
    updateActivity();
}

void initDiscord()
{
    discord::Activity activity{};
    activity.SetType(discord::ActivityType::Playing);
    activity.GetTimestamps().SetStart(0);
    activity.GetAssets().SetLargeText(hex::format("ImHex [{0}]", IMHEX_VERSION).c_str());
    activity.GetAssets().SetLargeImage(rpcdata::largeImage);

    updateTimestamp = true;
    hex::log::debug("Initialised Discord activity.");
}

void initEvents()
{
    const auto providerFocused = [](auto) { updateStatus(VIEWING_PROVIDER); };
    hex::EventProviderOpened::subscribe(providerFocused);
    hex::EventProviderClosed::subscribe(providerFocused);

    hex::EventViewOpened::subscribe([](const hex::View *view) {
        if (view == nullptr || !view->isFocused()) {
            return;
        }

        const auto name = view->getUnlocalizedName().get();
        if (name == views::ACHIEVEMENTS) {
            updateStatus(VIEWING_ACHIEVEMENTS);
        } else if (name == views::CONTENT_STORE) {
            updateStatus(VIEWING_CONTENT_STORE);
        } else if (name == views::THEME_MANAGER) {
            updateStatus(VIEWING_THEME_MANAGER);
        } else if (name == views::SETTINGS) {
            updateStatus(VIEWING_SETTINGS);
        } else if (name == views::ABOUT) {
            updateStatus(VIEWING_ABOUT);
        }
    });

    hex::EventFrameEnd::subscribe([] { getCore()->RunCallbacks(); });

    // Cleanup
    hex::EventWindowClosing::subscribe([](auto) {
        getCore()->ActivityManager().ClearActivity([](const discord::Result res) {
            if (res == discord::Result::Ok) {
                hex::log::info("Cleared Discord activity!");
            } else {
                hex::log::error("Failed to clear Discord activity. :c");
            }
        });
    });

    hex::log::debug("Registered events.");
}

void initSettings()
{
    {
        using namespace lang;

        ImHexSettings::setCategoryDescription(category, description);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", enabled, false);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showProvider, false);
        // .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showStatus, false);
        // .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showTimestamp, false);
        // .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", useRelativeTime, false);
        // .setEnabledCallback(settings::isEnabled);
    }

    ImHexSettings::onChange(lang::category, lang::enabled,
        [](const ImHexSettings::SettingsValue &val) {
            settings::enabled = val.get<bool>(true);
        });
    ImHexSettings::onChange(lang::category, lang::showProvider,
        [](const ImHexSettings::SettingsValue &val) {
            settings::showProvider = val.get<bool>(true);
        });
    ImHexSettings::onChange(lang::category, lang::showStatus,
        [](const ImHexSettings::SettingsValue &val) {
            settings::showStatus = val.get<bool>(true);
        });
    ImHexSettings::onChange(lang::category, lang::showTimestamp,
        [](const ImHexSettings::SettingsValue &val) {
            settings::showTimestamp = val.get<bool>(true);
        });
    ImHexSettings::onChange(lang::category, lang::useRelativeTime,
        [](const ImHexSettings::SettingsValue &val) {
            settings::useRelativeTime = val.get<bool>(true);
        });

    hex::log::debug("Initialised settings.");
}

IMHEX_PLUGIN_SETUP("ImHexDiscordRPC", "aoqia", "Adds Discord RPC to ImHex!")
{
    hex::log::debug("Using romfs: {}", romfs::name());

    for (const auto &path : romfs::list("lang")) {
        hex::ContentRegistry::Language::addLocalization(
            nlohmann::json::parse(romfs::get(path).string()));
    }

    discord::Core::Create(rpcdata::clientId, DiscordCreateFlags_Default,
        discordCore.get());

    initDiscord();
    initEvents();
    initSettings();
}