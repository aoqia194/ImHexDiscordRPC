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
    VIEWING_ACHIEVEMENTS,
    VIEWING_CONTENT_STORE,
    VIEWING_THEME_MANAGER,
    VIEWING_SETTINGS,
    VIEWING_ABOUT,
    PREVIOUS,
    MAX,
};

namespace rpcdata
{
    constexpr static discord::ClientId clientId = 1400779994891944026;
    constexpr static auto largeImage = "icon_1024";
} // namespace rpcdata

namespace views
{
    // clang-format off
    constexpr auto ABOUT             = "hex.builtin.view.help.about.name";
    constexpr auto ACHIEVEMENTS      = "hex.builtin.view.achievements.name";
    constexpr auto CONSTANTS         = "hex.builtin.view.constants.name";
    constexpr auto CONTENT_STORE     = "hex.builtin.view.store.name";
    constexpr auto DATA_PROCESSOR    = "hex.builtin.view.data_processor.name";
    constexpr auto HELP              = "hex.builtin.view.help.name";
    constexpr auto HEX_EDITOR        = "hex.builtin.view.hex_editor.name";
    constexpr auto LOG_CONSOLE       = "hex.builtin.view.log_console.name";
    constexpr auto PATCHES           = "hex.builtin.view.patches.name";
    constexpr auto PATTERN_DATA      = "hex.builtin.view.pattern_data.name";
    constexpr auto PATTERN_EDITOR    = "hex.builtin.view.pattern_editor.name";
    constexpr auto PROVIDER_SETTINGS = "hex.builtin.view.provider_settings.name";
    constexpr auto SETTINGS          = "hex.builtin.view.settings.name";
    constexpr auto THEME_MANAGER     = "hex.builtin.view.theme_manager.name";
    constexpr auto TUTORIAL          = "hex.builtin.view.tutorials.name";
    // clang-format on
} // namespace views

namespace lang
{
    constexpr const char *status[UserStatus::MAX] = {
        "hex.ImHexDiscordRPC.settings.status.none",
        "hex.ImHexDiscordRPC.settings.status.viewingAchievements",
        "hex.ImHexDiscordRPC.settings.status.viewingContentStore",
        "hex.ImHexDiscordRPC.settings.status.viewingThemeManager",
        "hex.ImHexDiscordRPC.settings.status.viewingSettings",
        "hex.ImHexDiscordRPC.settings.status.viewingAbout",
    };

    // clang-format off
    constexpr auto category        = "hex.ImHexDiscordRPC.settings";
    constexpr auto description     = "hex.ImHexDiscordRPC.settings.description";
    constexpr auto enabled         = "hex.ImHexDiscordRPC.settings.enabled";
    constexpr auto enabledTip      = "hex.ImHexDiscordRPC.settings.enabled.tooltip";
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
static UserStatus prevUserStatus = NONE;
std::unique_ptr<discord::Core> discordCore = nullptr;
std::unique_ptr<discord::Activity> discordActivity = nullptr;
static time_t startTimestamp;
static bool updateTimestamp = true;

//! Sets the state for a given Discord activity.
//! @param activity the activity
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

    // Set the secondary details text, which can be either the provider, status text or
    // none.
    if (hasProject && hasProvider) {
        state = hex::ImHexApi::Provider::get()->getName();
    } else if (userStatus != NONE) {
        state = hex::LocalizationManager::getLocalizedString(lang::status[userStatus]);
    }

    activity->SetDetails(details.c_str());
    activity->SetState(state.c_str());
}

//! Sets the timestamp for a given Discord activity.
//! @param activity the activity
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

void clearActivity()
{
    discordCore->ActivityManager().ClearActivity([](const discord::Result res) {
        if (res == discord::Result::Ok) {
            hex::log::info("Cleared Discord activity!");
        } else {
            hex::log::error("Failed to clear Discord activity. :c");
        }
    });
    hex::log::debug("Requested Discord to clear activity.");

    // Force run callbacks for situations where FrameEnd isn't called after.
    discordCore->RunCallbacks();
}

void updateActivity()
{
    if (!settings::enabled) {
        // hex::log::debug("Settings disabled, updating with blank activity.");
        // discordCore->ActivityManager().UpdateActivity({}, [](auto) {});
        return;
    }

    setActivityTimestamp(discordActivity.get());
    setActivityState(discordActivity.get());

    discordCore->ActivityManager().UpdateActivity(*discordActivity,
        [](const discord::Result res) {
            if (res == discord::Result::Ok) {
                hex::log::info("Discord activity updated!");
            } else {
                hex::log::error("Failed to update Discord activity. :c");
            }
        });
}

void updateStatus(const UserStatus status)
{
    prevUserStatus = userStatus;
    userStatus = status == PREVIOUS ? prevUserStatus : status;
    updateActivity();
}

void updateStatusFromView(const hex::View *view)
{
    if (!settings::showStatus) {
        if (userStatus != NONE) {
            updateStatus(NONE);
        }

        return;
    }

    if (view->getWindowOpenState()) {
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
        } else if (userStatus != NONE) {
            updateStatus(NONE);
        }
    } else {
        updateStatus(NONE);
    }
}

void initSettings()
{
    {
        using namespace lang;

        ImHexSettings::setCategoryDescription(category, description);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", enabled, false)
            .setTooltip(enabledTip);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showProject, false)
            .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showProvider, false)
            .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showStatus, false)
            .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", showTimestamp, false)
            .setEnabledCallback(settings::isEnabled);
        ImHexSettings::add<ImHexWidgets::Checkbox>(category, "", useRelativeTime, false)
            .setEnabledCallback(settings::isEnabled);
    }

    ImHexSettings::onChange(lang::category, lang::enabled,
        [](const ImHexSettings::SettingsValue &val) {
            const auto v = val.get<bool>(false);
            if (v == settings::enabled) {
                return;
            }

            settings::enabled = v;
            if (v) {
                updateActivity();
            } else {
                clearActivity();
            }
        });
    ImHexSettings::onChange(lang::category, lang::showProject,
        [](const ImHexSettings::SettingsValue &val) {
            const auto v = val.get<bool>(false);
            if (v == settings::showProject) {
                return;
            }

            settings::showProject = v;
            updateActivity();
        });
    ImHexSettings::onChange(lang::category, lang::showProvider,
        [](const ImHexSettings::SettingsValue &val) {
            const auto v = val.get<bool>(false);
            if (v == settings::showProvider) {
                return;
            }

            settings::showProvider = v;
            updateActivity();
        });
    ImHexSettings::onChange(lang::category, lang::showStatus,
        [](const ImHexSettings::SettingsValue &val) {
            const auto v = val.get<bool>(false);
            if (v == settings::showStatus) {
                return;
            }

            settings::showStatus = v;
            if (!v) {
                updateStatus(NONE);
            } else {
                if (userStatus == NONE && prevUserStatus != NONE) {
                    updateStatus(prevUserStatus);
                }

                updateActivity();
            }
        });
    ImHexSettings::onChange(lang::category, lang::showTimestamp,
        [](const ImHexSettings::SettingsValue &val) {
            const auto v = val.get<bool>(false);
            if (v == settings::showTimestamp) {
                return;
            }

            settings::showTimestamp = v;
            updateActivity();
        });
    ImHexSettings::onChange(lang::category, lang::useRelativeTime,
        [](const ImHexSettings::SettingsValue &val) {
            const auto v = val.get<bool>(false);
            if (v == settings::useRelativeTime) {
                return;
            }

            settings::useRelativeTime = v;
            updateActivity();
        });

    hex::log::debug("Initialised settings.");
}

void initEvents()
{
    // Provider

    const auto providerChange = [] {
        if (settings::useRelativeTime) {
            updateTimestamp = true;
        }

        updateActivity();
    };

    hex::EventProviderChanged::subscribe(providerChange);
    hex::EventProviderOpened::subscribe(providerChange);
    hex::EventProviderClosed::subscribe(providerChange);

    // View

    hex::EventViewOpened::subscribe(updateStatusFromView);
    hex::EventViewClosed::subscribe(updateStatusFromView);

    // Other

    hex::EventFrameEnd::subscribe([] {
        if (discordCore != nullptr) {
            discordCore->RunCallbacks();
        }
    });

    hex::EventImHexClosing::subscribe(clearActivity);

    hex::log::debug("Registered events.");
}

discord::Result initDiscord()
{
    discord::Core *core{};
    const auto res =
        discord::Core::Create(rpcdata::clientId, DiscordCreateFlags_Default, &core);
    if (res != discord::Result::Ok) {
        return res;
    }
    discordCore.reset(core);
    core = nullptr;
    hex::log::debug("Created Discord core.");

    // Seems to not do anything despite Discord saying it does???
    discordCore->SetLogHook(discord::LogLevel::Debug, [](auto level, auto msg) {
        using namespace discord;

        const auto msgFmt = "[Discord] {}";
        if (level == LogLevel::Debug) {
            hex::log::debug(msgFmt, msg);
        } else if (level == LogLevel::Info) {
            hex::log::info(msgFmt, msg);
        } else if (level == LogLevel::Warn) {
            hex::log::warn(msgFmt, msg);
        } else if (level == LogLevel::Error) {
            hex::log::error(msgFmt, msg);
        } else {
            hex::log::info("UNKNOWN LEVEL >> [Discord] {}", msg);
        }
    });

    discord::Activity activity{};
    activity.SetType(discord::ActivityType::Playing);
    activity.SetSupportedPlatforms(
        static_cast<std::uint32_t>(discord::ActivitySupportedPlatformFlags::Desktop));
    activity.GetAssets().SetLargeText(
        hex::format("ImHex [{}]", hex::ImHexApi::System::getImHexVersion().get(true))
            .c_str());
    activity.GetAssets().SetLargeImage(rpcdata::largeImage);
    discordActivity = std::make_unique<discord::Activity>(activity);

    updateTimestamp = true;
    hex::log::debug("Initialised Discord activity.");

    return discord::Result::Ok;
}

IMHEX_PLUGIN_SETUP("ImHexDiscordRPC", "aoqia", "Adds Discord RPC to ImHex!")
{
    hex::log::debug("Using romfs: {}", romfs::name());

    for (const auto &path : romfs::list("lang")) {
        hex::ContentRegistry::Language::addLocalization(
            nlohmann::json::parse(romfs::get(path).string()));
    }

    const auto res = initDiscord();
    if (res != discord::Result::Ok) {
        hex::log::error("Failed to create Discord core! Expected Ok got {}.",
            static_cast<int>(res));
        return;
    }

    initEvents();
    initSettings();
}