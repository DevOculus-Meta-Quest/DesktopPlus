#include "ConfigManager.h"

#include <algorithm>
#include <sstream>
#include <fstream>

#include "Util.h"
#include "OverlayManager.h"
#include "InterprocessMessaging.h"
#include "WindowList.h"
#include "DesktopPlusWinRT.h"

#ifdef DPLUS_UI
    #include "UIManager.h"
#else
    #include "WindowManager.h"
#endif

static ConfigManager g_ConfigManager;
static const std::string g_EmptyString;       //This way we can still return a const reference. Worth it? iunno

OverlayConfigData::OverlayConfigData()
{
    std::fill(std::begin(ConfigBool),   std::end(ConfigBool),   false);
    std::fill(std::begin(ConfigInt),    std::end(ConfigInt),    -1);
    std::fill(std::begin(ConfigFloat),  std::end(ConfigFloat),  0.0f);
    std::fill(std::begin(ConfigIntPtr), std::end(ConfigIntPtr), 0);

    //Default the transform matrices to zero as an indicator to reset them when possible later
    float matrix_zero[16] = { 0.0f };
    std::fill(std::begin(ConfigDetachedTransform), std::end(ConfigDetachedTransform), matrix_zero);
}

ConfigManager::ConfigManager() : m_IsSteamInstall(false)
{
    std::fill(std::begin(m_ConfigBool),  std::end(m_ConfigBool),  false);
    std::fill(std::begin(m_ConfigInt),   std::end(m_ConfigInt),   -1);
    std::fill(std::begin(m_ConfigFloat), std::end(m_ConfigFloat), 0.0f);
    //We don't need to initialize m_ConfigString

    //Init desktop count to the system metric, which already correct for most users
    m_ConfigInt[configid_int_state_interface_desktop_count] = ::GetSystemMetrics(SM_CMONITORS);

    //Init application path
    int buffer_size = 1024;
    DWORD read_length;
    WCHAR* buffer;

    while (true)
    {
        buffer = new WCHAR[buffer_size];

        read_length = ::GetModuleFileName(nullptr, buffer, buffer_size);

        if ( (read_length == buffer_size) && (::GetLastError() == ERROR_INSUFFICIENT_BUFFER) )
        {
            delete[] buffer;
            buffer_size += 1024;
        }
        else
        {
            break;
        }
    }

    if (::GetLastError() == ERROR_SUCCESS)
    {
        std::string path_str = StringConvertFromUTF16(buffer);

        //We got the full executable path, so let's get the folder part
        std::size_t pos = path_str.find_last_of("\\");
        m_ApplicationPath = path_str.substr(0, pos + 1);	//Includes trailing backslash
        m_ExecutableName  = path_str.substr(pos + 1, std::string::npos);

        //Somewhat naive way to check if this install is from Steam without using Steam API or shipping different binaries
        //Convert to lower first since there can be capitalization differences for the Steam directories
        std::wstring path_wstr = buffer;
        ::CharLowerBuff(buffer, (DWORD)path_wstr.length());
        path_wstr = buffer;

        m_IsSteamInstall = (path_wstr.find(L"\\steamapps\\common\\desktopplus\\desktopplus") != std::wstring::npos); 
    }

    delete[] buffer;

    //Check if UIAccess is enabled
    m_ConfigBool[configid_bool_state_misc_uiaccess_enabled] = IsUIAccessEnabled();
}

ConfigManager& ConfigManager::Get()
{
    return g_ConfigManager;
}

void ConfigManager::LoadOverlayProfile(const Ini& config, unsigned int overlay_id)
{
    OverlayConfigData& data = OverlayManager::Get().GetCurrentConfigData();
    unsigned int current_id = OverlayManager::Get().GetCurrentOverlayID();

    std::string section;

    if (overlay_id != UINT_MAX)
    {
        std::stringstream ss;
        ss << "Overlay" << overlay_id;

        section = ss.str();
    }
    else
    {
        section = "Overlay";
    }

    data.ConfigNameStr = config.ReadString(section.c_str(), "Name");

    //Determine if the name is one of the old default names when the NameIsCustom key is missing
    bool name_custom_default_value = false;
    bool do_set_auto_name = false;
    if (!config.KeyExists(section.c_str(), "NameIsCustom"))
    {
        do_set_auto_name = true; //Set overlay auto name later to override old default names

        //Check if it's empty or just "Dashboard" and skip it then
        if ((!data.ConfigNameStr.empty()) && (data.ConfigNameStr != "Dashboard"))
        {
            //Check if the name is "Overlay [Number]"
            std::stringstream ss(data.ConfigNameStr);
            std::string out_str;
            int out_int;

            ss >> out_str;

            if (out_str == "Overlay")
            {
                ss >> out_int;

                if (!ss.fail())
                {
                    //Check if any additional string can be extracted, which would mean it's custom despite starting like a default name
                    ss >> out_str;
                    name_custom_default_value = !ss.fail();
                }
                else //Number extraction failed, so custom
                {
                    name_custom_default_value = true;
                }
            }
            else //Doesn't start with "Overlay", so custom
            {
                name_custom_default_value = true;
            }
        }
    }

    data.ConfigBool[configid_bool_overlay_name_custom]                 = config.ReadBool(section.c_str(),   "NameIsCustom", name_custom_default_value);
    data.ConfigBool[configid_bool_overlay_enabled]                     = config.ReadBool(section.c_str(),   "Enabled", true);
    data.ConfigInt[configid_int_overlay_desktop_id]                    = config.ReadInt(section.c_str(),    "DesktopID", -2);
    data.ConfigInt[configid_int_overlay_capture_source]                = config.ReadInt(section.c_str(),    "CaptureSource", ovrl_capsource_desktop_duplication);
    data.ConfigInt[configid_int_overlay_winrt_desktop_id]              = config.ReadInt(section.c_str(),    "WinRTDesktopID", -2);
    data.ConfigStr[configid_str_overlay_winrt_last_window_title]       = config.ReadString(section.c_str(), "WinRTLastWindowTitle");
    data.ConfigStr[configid_str_overlay_winrt_last_window_exe_name]    = config.ReadString(section.c_str(), "WinRTLastWindowExeName");
    data.ConfigBool[configid_bool_overlay_width_unscaled]              = config.ReadBool(section.c_str(),   "WidthUnscaled", false);
    data.ConfigFloat[configid_float_overlay_width]                     = config.ReadInt(section.c_str(),    "Width", 350) / 100.0f; //165 but gets rescaled to that since WidthUnscaled is false
    data.ConfigFloat[configid_float_overlay_curvature]                 = config.ReadInt(section.c_str(),    "Curvature", 17) / 100.0f;
    data.ConfigFloat[configid_float_overlay_opacity]                   = config.ReadInt(section.c_str(),    "Opacity", 100) / 100.0f;
    data.ConfigFloat[configid_float_overlay_brightness]                = config.ReadInt(section.c_str(),    "Brightness", 100) / 100.0f;
    data.ConfigFloat[configid_float_overlay_offset_right]              = config.ReadInt(section.c_str(),    "OffsetRight", 0) / 100.0f;
    data.ConfigFloat[configid_float_overlay_offset_up]                 = config.ReadInt(section.c_str(),    "OffsetUp", 0) / 100.0f;
    data.ConfigFloat[configid_float_overlay_offset_forward]            = config.ReadInt(section.c_str(),    "OffsetForward", 0) / 100.0f;
    data.ConfigInt[configid_int_overlay_detached_display_mode]         = config.ReadInt(section.c_str(),    "DetachedDisplayMode", ovrl_dispmode_always);
    data.ConfigInt[configid_int_overlay_detached_origin]               = config.ReadInt(section.c_str(),    "DetachedOrigin", ovrl_origin_room);

    data.ConfigInt[configid_int_overlay_crop_x]                        = config.ReadInt(section.c_str(),  "CroppingX", 0);
    data.ConfigInt[configid_int_overlay_crop_y]                        = config.ReadInt(section.c_str(),  "CroppingY", 0);
    data.ConfigInt[configid_int_overlay_crop_width]                    = config.ReadInt(section.c_str(),  "CroppingWidth", -1);
    data.ConfigInt[configid_int_overlay_crop_height]                   = config.ReadInt(section.c_str(),  "CroppingHeight", -1);

    data.ConfigInt[configid_int_overlay_3D_mode]                       = config.ReadInt(section.c_str(),  "3DMode", ovrl_3Dmode_none);
    data.ConfigBool[configid_bool_overlay_3D_swapped]                  = config.ReadBool(section.c_str(), "3DSwapped", false);
    data.ConfigBool[configid_bool_overlay_gazefade_enabled]            = config.ReadBool(section.c_str(), "GazeFade", false);
    data.ConfigFloat[configid_float_overlay_gazefade_distance]         = config.ReadInt(section.c_str(),  "GazeFadeDistance", 0) / 100.0f;
    data.ConfigFloat[configid_float_overlay_gazefade_rate]             = config.ReadInt(section.c_str(),  "GazeFadeRate", 100) / 100.0f;
    data.ConfigFloat[configid_float_overlay_gazefade_opacity]          = config.ReadInt(section.c_str(),  "GazeFadeOpacity", 0) / 100.0f;
    data.ConfigInt[configid_int_overlay_update_limit_override_mode]    = config.ReadInt(section.c_str(),  "UpdateLimitModeOverride", update_limit_mode_off);
    data.ConfigFloat[configid_float_overlay_update_limit_override_ms]  = config.ReadInt(section.c_str(),  "UpdateLimitMS", 0) / 100.0f;
    data.ConfigInt[configid_int_overlay_update_limit_override_fps]     = config.ReadInt(section.c_str(),  "UpdateLimitFPS", update_limit_fps_30);
    data.ConfigBool[configid_bool_overlay_input_enabled]               = config.ReadBool(section.c_str(), "InputEnabled", true);
    data.ConfigInt[configid_int_overlay_group_id]                      = config.ReadInt(section.c_str(),  "GroupID", 0);
    data.ConfigBool[configid_bool_overlay_update_invisible]            = config.ReadBool(section.c_str(), "UpdateInvisible", false);

    data.ConfigBool[configid_bool_overlay_floatingui_enabled]          = config.ReadBool(section.c_str(), "ShowFloatingUI", true);
    data.ConfigBool[configid_bool_overlay_floatingui_desktops_enabled] = config.ReadBool(section.c_str(), "ShowDesktopButtons", (current_id == k_ulOverlayID_Dashboard));
    data.ConfigBool[configid_bool_overlay_actionbar_enabled]           = config.ReadBool(section.c_str(), "ShowActionBar", false);
    data.ConfigBool[configid_bool_overlay_actionbar_order_use_global]  = config.ReadBool(section.c_str(), "ActionBarOrderUseGlobal", true);

    //Restore WinRT Capture state if possible
    if ( (data.ConfigInt[configid_int_overlay_winrt_desktop_id] == -2) && (!data.ConfigStr[configid_str_overlay_winrt_last_window_title].empty()) )
    {
        HWND window = WindowInfo::FindClosestWindowForTitle(data.ConfigStr[configid_str_overlay_winrt_last_window_title], data.ConfigStr[configid_str_overlay_winrt_last_window_exe_name]);
        data.ConfigIntPtr[configid_intptr_overlay_state_winrt_hwnd] = (intptr_t)window;

        //If we found a new match, adjust last window title and update the overlay name later (we want to keep the old name if the window is gone though)
        if (window != nullptr)
        {
            WindowInfo info(window);
            data.ConfigStr[configid_str_overlay_winrt_last_window_title] = StringConvertFromUTF16(info.Title.c_str());
            //ExeName is not gonna change

            do_set_auto_name = true;
        }
    }

    //Disable settings which are invalid for the dashboard overlay
    if (current_id == k_ulOverlayID_Dashboard)
    {
        data.ConfigBool[configid_bool_overlay_gazefade_enabled] = false;

        //If single desktop mirroring is active, set default desktop ID to 0 (in combined desktop mode it's taken care of during ApplySettingCrop())
        if ( (data.ConfigInt[configid_int_overlay_desktop_id] == -2) && (m_ConfigBool[configid_bool_performance_single_desktop_mirroring]) )
        {
            data.ConfigInt[configid_int_overlay_desktop_id] = 0;
        }
    }
    else if (m_ConfigBool[configid_bool_performance_single_desktop_mirroring])
    {
        //If single desktop mirroring is active, set desktop ID to dashboard one
        data.ConfigInt[configid_int_overlay_desktop_id] = OverlayManager::Get().GetConfigData(k_ulOverlayID_Dashboard).ConfigInt[configid_int_overlay_desktop_id];
    }

    //v2.5.5 introduced seated position origin, shifting origin IDs. If the seated transform doesn't exist we assume it was saved with the previous origin enum order
    if ( (data.ConfigInt[configid_int_overlay_detached_origin] >= ovrl_origin_seated_universe) && (!config.KeyExists(section.c_str(), "DetachedTransformSeatedPosition")) )
    {
        data.ConfigInt[configid_int_overlay_detached_origin] += 1; 
    }

    //Default the transform matrices to zero
    float matrix_zero[16] = { 0.0f };
    std::fill(std::begin(data.ConfigDetachedTransform), std::end(data.ConfigDetachedTransform), matrix_zero);

    std::string transform_str; //Only set these when it's really present in the file, or else it defaults to identity instead of zero
    transform_str = config.ReadString(section.c_str(), "DetachedTransformPlaySpace");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_room] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformHMDFloor");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_hmd_floor] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformSeatedPosition");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_seated_universe] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformDashboard");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_dashboard] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformHMD");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_hmd] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformRightHand");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_right_hand] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformLeftHand");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_left_hand] = transform_str;

    transform_str = config.ReadString(section.c_str(), "DetachedTransformAux");
    if (!transform_str.empty())
        data.ConfigDetachedTransform[ovrl_origin_aux] = transform_str;

    //Load action order
    auto& action_order_global = ConfigManager::GetActionMainBarOrder();
    auto& action_order = data.ConfigActionBarOrder;
    action_order.clear();
    std::string order_str = config.ReadString(section.c_str(), "ActionBarOrderCustom");
    std::stringstream ss(order_str);
    int id;
    bool visible;
    char sep;

    for (;;)
    {
        ss >> id >> visible >> sep;

        if (ss.fail())
            break;

        action_order.push_back({ (ActionID)id, visible });
    }

    //If there is a mismatch or it's fully missing, reset to global
    if (action_order.size() != action_order_global.size())
    {
        action_order = action_order_global;
    }

    //Migrate now invalid curvature value
    if (data.ConfigFloat[configid_float_overlay_curvature] == -1.0f)
    {
        data.ConfigFloat[configid_float_overlay_curvature] = 0.17f; //17% is about what the default dashboard curvature is at the default width
    }

    //If transforms still contain scale (up until v2.4.2), fix them up
    if (!data.ConfigBool[configid_bool_overlay_width_unscaled])
    {
        if (current_id == k_ulOverlayID_Dashboard)
        {
            data.ConfigFloat[configid_float_overlay_width] *= 0.4725f; //Not exact dashboard scale (that can vary anyways), but converts old default value to new default
        }
        else
        {
            for (size_t i = ovrl_origin_room; i < ovrl_origin_MAX; ++i)
            {
                RemoveScaleFromTransform(data.ConfigDetachedTransform[i], 
                                         (i == data.ConfigInt[configid_int_overlay_detached_origin]) ? &data.ConfigFloat[configid_float_overlay_width] : nullptr);
            }
        }

        data.ConfigBool[configid_bool_overlay_width_unscaled] = true;
    }

    #ifdef DPLUS_UI
    //When loading an UI overlay, send config state over to ensure the correct process has rendering access even if the UI was restarted at some point
    if (data.ConfigInt[configid_int_overlay_capture_source] == ovrl_capsource_ui)
    {
        IPCManager::Get().PostMessageToDashboardApp(ipcmsg_set_config, ConfigManager::Get().GetWParamForConfigID(configid_int_state_overlay_current_id_override), (int)overlay_id);
        IPCManager::Get().PostMessageToDashboardApp(ipcmsg_set_config, ConfigManager::Get().GetWParamForConfigID(configid_int_overlay_capture_source), ovrl_capsource_ui);
        IPCManager::Get().PostMessageToDashboardApp(ipcmsg_set_config, ConfigManager::Get().GetWParamForConfigID(configid_int_state_overlay_current_id_override), -1);

        UIManager::Get()->GetPerformanceWindow().ScheduleOverlaySharedTextureUpdate();
    }

    //Set auto name if the NameIsCustom key was missing entirely or there's a new window match
    if (do_set_auto_name)
    {
        OverlayManager::Get().SetCurrentOverlayNameAuto();
    }
    #endif
}

void ConfigManager::SaveOverlayProfile(Ini& config, unsigned int overlay_id)
{
    const OverlayConfigData& data = OverlayManager::Get().GetCurrentConfigData();

    std::string section;

    if (overlay_id != UINT_MAX)
    {
        std::stringstream ss;
        ss << "Overlay" << overlay_id;

        section = ss.str();
    }
    else
    {
        section = "Overlay";
    }

    config.WriteString(section.c_str(), "Name", data.ConfigNameStr.c_str());

    config.WriteBool(section.c_str(), "NameIsCustom",           data.ConfigBool[configid_bool_overlay_name_custom]);
    config.WriteBool(section.c_str(), "Enabled",                data.ConfigBool[configid_bool_overlay_enabled]);
    config.WriteInt( section.c_str(), "DesktopID",              data.ConfigInt[configid_int_overlay_desktop_id]);
    config.WriteInt( section.c_str(), "CaptureSource",          data.ConfigInt[configid_int_overlay_capture_source]);
    config.WriteBool(section.c_str(), "WidthUnscaled",          data.ConfigBool[configid_bool_overlay_width_unscaled]);
    config.WriteInt( section.c_str(), "Width",              int(data.ConfigFloat[configid_float_overlay_width]           * 100.0f));
    config.WriteInt( section.c_str(), "Curvature",          int(data.ConfigFloat[configid_float_overlay_curvature]       * 100.0f));
    config.WriteInt( section.c_str(), "Opacity",            int(data.ConfigFloat[configid_float_overlay_opacity]         * 100.0f));
    config.WriteInt( section.c_str(), "Brightness",         int(data.ConfigFloat[configid_float_overlay_brightness]      * 100.0f));
    config.WriteInt( section.c_str(), "OffsetRight",        int(data.ConfigFloat[configid_float_overlay_offset_right]    * 100.0f));
    config.WriteInt( section.c_str(), "OffsetUp",           int(data.ConfigFloat[configid_float_overlay_offset_up]       * 100.0f));
    config.WriteInt( section.c_str(), "OffsetForward",      int(data.ConfigFloat[configid_float_overlay_offset_forward]  * 100.0f));
    config.WriteInt( section.c_str(), "DetachedDisplayMode",    data.ConfigInt[configid_int_overlay_detached_display_mode]);
    config.WriteInt( section.c_str(), "DetachedOrigin",         data.ConfigInt[configid_int_overlay_detached_origin]);

    config.WriteInt( section.c_str(), "CroppingX",              data.ConfigInt[configid_int_overlay_crop_x]);
    config.WriteInt( section.c_str(), "CroppingY",              data.ConfigInt[configid_int_overlay_crop_y]);
    config.WriteInt( section.c_str(), "CroppingWidth",          data.ConfigInt[configid_int_overlay_crop_width]);
    config.WriteInt( section.c_str(), "CroppingHeight",         data.ConfigInt[configid_int_overlay_crop_height]);

    config.WriteInt( section.c_str(), "3DMode",                 data.ConfigInt[configid_int_overlay_3D_mode]);
    config.WriteBool(section.c_str(), "3DSwapped",              data.ConfigBool[configid_bool_overlay_3D_swapped]);
    config.WriteBool(section.c_str(), "GazeFade",               data.ConfigBool[configid_bool_overlay_gazefade_enabled]);
    config.WriteInt( section.c_str(), "GazeFadeDistance",   int(data.ConfigFloat[configid_float_overlay_gazefade_distance]  * 100.0f));
    config.WriteInt( section.c_str(), "GazeFadeRate",       int(data.ConfigFloat[configid_float_overlay_gazefade_rate]      * 100.0f));
    config.WriteInt( section.c_str(), "GazeFadeOpacity",    int(data.ConfigFloat[configid_float_overlay_gazefade_opacity]   * 100.0f));
    config.WriteInt( section.c_str(), "UpdateLimitModeOverride",data.ConfigInt[configid_int_overlay_update_limit_override_mode]);
    config.WriteInt( section.c_str(), "UpdateLimitMS",      int(data.ConfigFloat[configid_float_overlay_update_limit_override_ms] * 100.0f));
    config.WriteInt( section.c_str(), "UpdateLimitFPS",         data.ConfigInt[configid_int_overlay_update_limit_override_fps]);
    config.WriteBool(section.c_str(), "InputEnabled",           data.ConfigBool[configid_bool_overlay_input_enabled]);
    config.WriteInt( section.c_str(), "GroupID",                data.ConfigInt[configid_int_overlay_group_id]);
    config.WriteBool(section.c_str(), "UpdateInvisible",        data.ConfigBool[configid_bool_overlay_update_invisible]);

    config.WriteBool(section.c_str(), "ShowFloatingUI",          data.ConfigBool[configid_bool_overlay_floatingui_enabled]);
    config.WriteBool(section.c_str(), "ShowDesktopButtons",      data.ConfigBool[configid_bool_overlay_floatingui_desktops_enabled]);
    config.WriteBool(section.c_str(), "ShowActionBar",           data.ConfigBool[configid_bool_overlay_actionbar_enabled]);
    config.WriteBool(section.c_str(), "ActionBarOrderUseGlobal", data.ConfigBool[configid_bool_overlay_actionbar_order_use_global]);

    config.WriteString(section.c_str(), "DetachedTransformPlaySpace",      data.ConfigDetachedTransform[ovrl_origin_room].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformHMDFloor",       data.ConfigDetachedTransform[ovrl_origin_hmd_floor].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformSeatedPosition", data.ConfigDetachedTransform[ovrl_origin_seated_universe].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformDashboard",      data.ConfigDetachedTransform[ovrl_origin_dashboard].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformHMD",            data.ConfigDetachedTransform[ovrl_origin_hmd].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformRightHand",      data.ConfigDetachedTransform[ovrl_origin_right_hand].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformLeftHand",       data.ConfigDetachedTransform[ovrl_origin_left_hand].toString().c_str());
    config.WriteString(section.c_str(), "DetachedTransformAux",            data.ConfigDetachedTransform[ovrl_origin_aux].toString().c_str());

    //Save WinRT Capture state
    HWND window_handle = (HWND)data.ConfigIntPtr[configid_intptr_overlay_state_winrt_hwnd];
    std::string last_window_title, last_window_exe_name;

    if (window_handle != nullptr)
    {
        WindowInfo info(window_handle);
        info.ExeName = WindowInfo::GetExeName(window_handle);

        last_window_title    = StringConvertFromUTF16(info.Title.c_str());
        last_window_exe_name = info.ExeName;
    }
    else //Save last known title and exe name even when handle is nullptr so we can still restore the window on the next load if it happens to exist
    {
        last_window_title    = data.ConfigStr[configid_str_overlay_winrt_last_window_title];
        last_window_exe_name = data.ConfigStr[configid_str_overlay_winrt_last_window_exe_name];
    }

    config.WriteString(section.c_str(), "WinRTLastWindowTitle",   last_window_title.c_str());
    config.WriteString(section.c_str(), "WinRTLastWindowExeName", last_window_exe_name.c_str());
    config.WriteInt(section.c_str(), "WinRTDesktopID", data.ConfigInt[configid_int_overlay_winrt_desktop_id]);

    //Save action order
    std::stringstream ss;

    for (auto& data_order : data.ConfigActionBarOrder)
    {
        ss << data_order.action_id << ' ' << data_order.visible << ";";
    }

    config.WriteString(section.c_str(), "ActionBarOrderCustom", ss.str().c_str());
}

bool ConfigManager::LoadConfigFromFile()
{
    std::wstring wpath = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/config.ini").c_str() );
    bool existed = FileExists(wpath.c_str());

    //In preparation for NewUI/3.0+ Desktop+ migrating legacy config files and using the normal "config.ini" filename, 
    //load "config_legacy.ini" instead if it exists (will be renamed to this before migration)
    std::wstring wpath_legacy = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/config_legacy.ini").c_str() );
    if (FileExists(wpath_legacy.c_str()))
    {
        wpath = wpath_legacy;
        existed = true;
    }

    //If config.ini doesn't exist (yet), load from config_default.ini instead, which hopefully does (would still work to a lesser extent though)
    if (!existed)
    {
        wpath = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/config_default.ini").c_str() );
    }

    Ini config(wpath.c_str());

    m_ConfigBool[configid_bool_interface_no_ui]                              = config.ReadBool("Interface", "NoUIAutoLaunch", false);
    m_ConfigBool[configid_bool_interface_no_notification_icon]               = config.ReadBool("Interface", "NoNotificationIcon", false);
    m_ConfigBool[configid_bool_interface_large_style]                        = config.ReadBool("Interface", "DisplaySizeLarge", false);
    m_ConfigInt[configid_int_interface_overlay_current_id]                   = config.ReadInt( "Interface", "OverlayCurrentID", 0);
    m_ConfigInt[configid_int_interface_mainbar_desktop_listing]              = config.ReadInt( "Interface", "DesktopButtonCyclingMode", mainbar_desktop_listing_individual);
    m_ConfigBool[configid_bool_interface_mainbar_desktop_include_all]        = config.ReadBool("Interface", "DesktopButtonIncludeAll", false);

    //Read color string and store it interpreted as signed int
    unsigned int rgba = std::stoul(config.ReadString("Interface", "EnvironmentBackgroundColor", "00000080"), nullptr, 16);
    m_ConfigInt[configid_int_interface_background_color] = *(int*)&rgba;

    m_ConfigInt[configid_int_interface_background_color_display_mode]        = config.ReadInt( "Interface", "EnvironmentBackgroundColorDisplayMode", ui_bgcolor_dispmode_never);
    m_ConfigBool[configid_bool_interface_dim_ui]                             = config.ReadBool("Interface", "DimUI", false);
    m_ConfigFloat[configid_float_interface_last_vr_ui_scale]                 = config.ReadInt( "Interface", "LastVRUIScale", 100) / 100.0f;
    m_ConfigBool[configid_bool_interface_warning_compositor_res_hidden]      = config.ReadBool("Interface", "WarningCompositorResolutionHidden", false);
    m_ConfigBool[configid_bool_interface_warning_compositor_quality_hidden]  = config.ReadBool("Interface", "WarningCompositorQualityHidden", false);
    m_ConfigBool[configid_bool_interface_warning_process_elevation_hidden]   = config.ReadBool("Interface", "WarningProcessElevationHidden", false);
    m_ConfigBool[configid_bool_interface_warning_elevated_mode_hidden]       = config.ReadBool("Interface", "WarningElevatedModeHidden", false);
    m_ConfigBool[configid_bool_interface_warning_welcome_hidden]             = config.ReadBool("Interface", "WarningWelcomeHidden", false);
    m_ConfigInt[configid_int_interface_wmr_ignore_vscreens]                  = config.ReadInt( "Interface", "WMRIgnoreVScreens", -1);

    OverlayManager::Get().SetCurrentOverlayID(m_ConfigInt[configid_int_interface_overlay_current_id]);

    //Load action order
    auto& action_order = m_ActionManager.GetActionMainBarOrder();
    action_order.clear();
    std::string order_str = config.ReadString("Interface", "ActionOrder");
    std::stringstream ss(order_str);
    int id;
    bool visible;
    char sep;

    for (;;)
    {
        ss >> id >> visible >> sep;

        if (ss.fail())
            break;

        action_order.push_back({ (ActionID)id, visible });
    }

	m_ConfigInt[configid_int_input_go_home_action_id]                       = config.ReadInt( "Input", "GoHomeButtonActionID", 0);
	m_ConfigInt[configid_int_input_go_back_action_id]                       = config.ReadInt( "Input", "GoBackButtonActionID", 0);
	m_ConfigInt[configid_int_input_shortcut01_action_id]                    = config.ReadInt( "Input", "GlobalShortcut01ActionID", 0);
	m_ConfigInt[configid_int_input_shortcut02_action_id]                    = config.ReadInt( "Input", "GlobalShortcut02ActionID", 0);
	m_ConfigInt[configid_int_input_shortcut03_action_id]                    = config.ReadInt( "Input", "GlobalShortcut03ActionID", 0);

	m_ConfigInt[configid_int_input_hotkey01_modifiers]                      = config.ReadInt( "Input", "GlobalHotkey01Modifiers", 0);
	m_ConfigInt[configid_int_input_hotkey01_keycode]                        = config.ReadInt( "Input", "GlobalHotkey01KeyCode",   0);
	m_ConfigInt[configid_int_input_hotkey01_action_id]                      = config.ReadInt( "Input", "GlobalHotkey01ActionID",  0);
	m_ConfigInt[configid_int_input_hotkey02_modifiers]                      = config.ReadInt( "Input", "GlobalHotkey02Modifiers", 0);
	m_ConfigInt[configid_int_input_hotkey02_keycode]                        = config.ReadInt( "Input", "GlobalHotkey02KeyCode",   0);
	m_ConfigInt[configid_int_input_hotkey02_action_id]                      = config.ReadInt( "Input", "GlobalHotkey02ActionID",  0);
	m_ConfigInt[configid_int_input_hotkey03_modifiers]                      = config.ReadInt( "Input", "GlobalHotkey03Modifiers", 0);
	m_ConfigInt[configid_int_input_hotkey03_keycode]                        = config.ReadInt( "Input", "GlobalHotkey03KeyCode",   0);
	m_ConfigInt[configid_int_input_hotkey03_action_id]                      = config.ReadInt( "Input", "GlobalHotkey03ActionID",  0);

    m_ConfigFloat[configid_float_input_detached_interaction_max_distance]   = config.ReadInt( "Input", "DetachedInteractionMaxDistance", 30) / 100.0f;
    m_ConfigBool[configid_bool_input_global_hmd_pointer]                    = config.ReadBool("Input", "GlobalHMDPointer", false);
    m_ConfigFloat[configid_float_input_global_hmd_pointer_max_distance]     = config.ReadInt( "Input", "GlobalHMDPointerMaxDistance", 0) / 100.0f;

    m_ConfigBool[configid_bool_input_mouse_render_cursor]              = config.ReadBool("Mouse", "RenderCursor", true);
    m_ConfigBool[configid_bool_input_mouse_render_intersection_blob]   = config.ReadBool("Mouse", "RenderIntersectionBlob", false);
	m_ConfigInt[configid_int_input_mouse_dbl_click_assist_duration_ms] = config.ReadInt( "Mouse", "DoubleClickAssistDuration", -1);
	m_ConfigBool[configid_bool_input_mouse_hmd_pointer_override]       = config.ReadBool("Mouse", "HMDPointerOverride", true);

    m_ConfigBool[configid_bool_input_keyboard_helper_enabled]          = config.ReadBool("Keyboard", "EnableKeyboardHelper", true);
    m_ConfigFloat[configid_float_input_keyboard_detached_size]         = config.ReadInt( "Keyboard", "KeyboardDetachedSize", 100) / 100.0f;

    m_ConfigBool[configid_bool_windows_auto_focus_scene_app_dashboard] = config.ReadBool("Windows", "AutoFocusSceneAppDashboard", false);
    m_ConfigBool[configid_bool_windows_winrt_auto_focus]               = config.ReadBool("Windows", "WinRTAutoFocus", true);
    m_ConfigBool[configid_bool_windows_winrt_keep_on_screen]           = config.ReadBool("Windows", "WinRTKeepOnScreen", true);
    m_ConfigInt[configid_int_windows_winrt_dragging_mode]              = config.ReadInt( "Windows", "WinRTDraggingMode", window_dragging_overlay);
    m_ConfigBool[configid_bool_windows_winrt_auto_size_overlay]        = config.ReadBool("Windows", "WinRTAutoSizeOverlay", false);
    m_ConfigBool[configid_bool_windows_winrt_auto_focus_scene_app]     = config.ReadBool("Windows", "WinRTAutoFocusSceneApp", false);

    m_ConfigInt[configid_int_performance_update_limit_mode]              = config.ReadInt( "Performance", "UpdateLimitMode", update_limit_mode_off);
    m_ConfigFloat[configid_float_performance_update_limit_ms]            = config.ReadInt( "Performance", "UpdateLimitMS", 0) / 100.0f;
    m_ConfigInt[configid_int_performance_update_limit_fps]               = config.ReadInt( "Performance", "UpdateLimitFPS", update_limit_fps_30);
    m_ConfigBool[configid_bool_performance_rapid_laser_pointer_updates]  = config.ReadBool("Performance", "RapidLaserPointerUpdates", false);
    m_ConfigBool[configid_bool_performance_single_desktop_mirroring]     = config.ReadBool("Performance", "SingleDesktopMirroring", false);
    m_ConfigBool[configid_bool_performance_monitor_large_style]          = config.ReadBool("Performance", "PerformanceMonitorStyleLarge", true);
    m_ConfigBool[configid_bool_performance_monitor_show_graphs]          = config.ReadBool("Performance", "PerformanceMonitorShowGraphs", true);
    m_ConfigBool[configid_bool_performance_monitor_show_time]            = config.ReadBool("Performance", "PerformanceMonitorShowTime", false);
    m_ConfigBool[configid_bool_performance_monitor_show_cpu]             = config.ReadBool("Performance", "PerformanceMonitorShowCPU", true);
    m_ConfigBool[configid_bool_performance_monitor_show_gpu]             = config.ReadBool("Performance", "PerformanceMonitorShowGPU", true);
    m_ConfigBool[configid_bool_performance_monitor_show_fps]             = config.ReadBool("Performance", "PerformanceMonitorShowFPS", true);
    m_ConfigBool[configid_bool_performance_monitor_show_battery]         = config.ReadBool("Performance", "PerformanceMonitorShowBattery", true);
    m_ConfigBool[configid_bool_performance_monitor_show_trackers]        = config.ReadBool("Performance", "PerformanceMonitorShowTrackers", true);
    m_ConfigBool[configid_bool_performance_monitor_show_vive_wireless]   = config.ReadBool("Performance", "PerformanceMonitorShowViveWireless", false);
    m_ConfigBool[configid_bool_performance_monitor_disable_gpu_counters] = config.ReadBool("Performance", "PerformanceMonitorDisableGPUCounters", false);

    m_ConfigBool[configid_bool_misc_no_steam]                        = config.ReadBool("Misc", "NoSteam", false);
    m_ConfigBool[configid_bool_misc_uiaccess_was_enabled]            = config.ReadBool("Misc", "UIAccessWasEnabled", false);
    m_ConfigBool[configid_bool_misc_apply_steamvr2_dashboard_offset] = config.ReadBool("Misc", "ApplySteamVR2DashboardOffset", true);

    //Load custom actions (this is where using ini feels dumb, but it still kinda works)
    auto& custom_actions = m_ActionManager.GetCustomActions();
    custom_actions.clear();
    int custom_action_count = config.ReadInt("CustomActions", "Count", 0);

    for (int i = 0; i < custom_action_count; ++i)
    {
        std::string action_ini_name = "Action" + std::to_string(i);
        CustomAction action;
        action.Name = config.ReadString("CustomActions", (action_ini_name + "Name").c_str(), action_ini_name.c_str());
        action.FunctionType = ActionManager::ParseCustomActionFunctionString( config.ReadString("CustomActions", (action_ini_name + "FunctionType").c_str()) );

        switch (action.FunctionType)
        {
            case caction_press_keys:
            {
                action.KeyCodes[0] = config.ReadInt( "CustomActions", (action_ini_name + "KeyCode1").c_str(),   0);
                action.KeyCodes[1] = config.ReadInt( "CustomActions", (action_ini_name + "KeyCode2").c_str(),   0);
                action.KeyCodes[2] = config.ReadInt( "CustomActions", (action_ini_name + "KeyCode3").c_str(),   0);
                action.IntID       = config.ReadBool("CustomActions", (action_ini_name + "ToggleKeys").c_str(), false);
                break;
            }
            case caction_type_string:
            {
                action.StrMain = config.ReadString("CustomActions", (action_ini_name + "TypeString").c_str());
                break;
            }
            case caction_launch_application:
            {
                action.StrMain = config.ReadString("CustomActions", (action_ini_name + "ExecutablePath").c_str());
                action.StrArg  = config.ReadString("CustomActions", (action_ini_name + "ExecutableArg").c_str());
                break;
            }
            case caction_toggle_overlay_enabled_state:
            {
                action.IntID = config.ReadInt("CustomActions", (action_ini_name + "OverlayID").c_str(), 0);
                break;
            }
        }
        
        #ifdef DPLUS_UI
            action.IconFilename = config.ReadString("CustomActions", (action_ini_name + "IconFilename").c_str());
        #endif

        custom_actions.push_back(action);
    }

    //Provide default for empty order list
    if (action_order.empty()) 
    {
        for (int i = action_show_keyboard; i < action_built_in_MAX; ++i)
        {
            action_order.push_back({(ActionID)i, false});
        }

        for (int i = action_custom; i < action_custom + custom_actions.size(); ++i)
        {
            action_order.push_back({(ActionID)i, false});
        }
    }
    else
    {
        //Validate order list in case some manual editing was made
        action_order.erase(std::remove_if(action_order.begin(),
                                          action_order.end(),
                                          [](const ActionMainBarOrderData& data) { return !ActionManager::Get().IsActionIDValid(data.action_id); }),
                                          action_order.end());

        //Automatically add actions if they're missing
        bool is_action_present;

        for (int i = action_show_keyboard; i < action_custom + custom_actions.size(); ++i)
        {
            is_action_present = false;

            for (const auto order_data : action_order)
            {
                if (order_data.action_id == i)
                {
                    is_action_present = true;
                    break;
                }
            }

            if (!is_action_present)
            {
                action_order.push_back({(ActionID)i, false});
            }

            //After built-in actions are checked, jump to custom range
            if (i == action_built_in_MAX - 1)
            {
                i = action_custom - 1;
            }
        }
    }

    //Validate action IDs for controller bindings too
    if (!ActionManager::Get().IsActionIDValid((ActionID)m_ConfigInt[configid_int_input_go_home_action_id]))
    {
        m_ConfigInt[configid_int_input_go_home_action_id] = action_none;
    }
    if (!ActionManager::Get().IsActionIDValid((ActionID)m_ConfigInt[configid_int_input_go_back_action_id]))
    {
        m_ConfigInt[configid_int_input_go_back_action_id] = action_none;
    }
    if (!ActionManager::Get().IsActionIDValid((ActionID)m_ConfigInt[configid_int_input_shortcut01_action_id]))
    {
        m_ConfigInt[configid_int_input_shortcut01_action_id] = action_none;
    }
    if (!ActionManager::Get().IsActionIDValid((ActionID)m_ConfigInt[configid_int_input_shortcut02_action_id]))
    {
        m_ConfigInt[configid_int_input_shortcut02_action_id] = action_none;
    }
    if (!ActionManager::Get().IsActionIDValid((ActionID)m_ConfigInt[configid_int_input_shortcut03_action_id]))
    {
        m_ConfigInt[configid_int_input_shortcut03_action_id] = action_none;
    }

    //Apply render cursor setting for WinRT Capture
    #ifndef DPLUS_UI
        if (DPWinRT_IsCaptureCursorEnabledPropertySupported())
            DPWinRT_SetCaptureCursorEnabled(m_ConfigBool[configid_bool_input_mouse_render_cursor]);
    #endif

    #ifndef DPLUS_UI
        WindowManager::Get().UpdateConfigState();
    #endif

    //Query elevated mode state
    m_ConfigBool[configid_bool_state_misc_elevated_mode_active] = IPCManager::IsElevatedModeProcessRunning();

    //v2.5.2 fixed UI dimming setting being written from the wrong value.
    //Best way to work around it is to not trust this setting when seated position (v2.5.5+) doesn't exist in the file
    if (!config.KeyExists("Overlay0", "DetachedTransformSeatedPosition"))
    {
        m_ConfigBool[configid_bool_interface_dim_ui] = false;
    }

    //Load last used overlay config
    LoadMultiOverlayProfile(config);

    return existed; //We use default values if it doesn't, but still return if the file existed
}

void ConfigManager::LoadMultiOverlayProfile(const Ini& config, bool clear_existing_overlays)
{
    unsigned int current_overlay_old = OverlayManager::Get().GetCurrentOverlayID();

    unsigned int overlay_id = 1; //Don't load dashboard overlay unless we're clearing existing overlays

    if (clear_existing_overlays)
    {
        OverlayManager::Get().RemoveAllOverlays(); //This doesn't remove the dashboard overlay, but it will be overwritten later

        overlay_id = k_ulOverlayID_Dashboard; //Load dashboard overlay

        //If "Overlay0" doesn't exist (transitioning from old config), load from "Overlay" (or try to, in which case we at least get proper defaults)
        if (!config.SectionExists("Overlay0"))
        {
            OverlayManager::Get().SetCurrentOverlayID(k_ulOverlayID_Dashboard);
            LoadOverlayProfile(config, UINT_MAX);
            overlay_id++;
        }
    }

    std::stringstream ss;
    ss << "Overlay" << overlay_id;

    //Load all sequential overlay sections that exist
    while (config.SectionExists(ss.str().c_str()))
    {
        if (overlay_id != 0)
        {
            OverlayManager::Get().AddOverlay(OverlayConfigData());
            OverlayManager::Get().SetCurrentOverlayID(OverlayManager::Get().GetOverlayCount() - 1);
        }
        else
        {
            OverlayManager::Get().SetCurrentOverlayID(k_ulOverlayID_Dashboard);
        }

        LoadOverlayProfile(config, overlay_id);

        overlay_id++;

        ss = std::stringstream();
        ss << "Overlay" << overlay_id;
    }

    OverlayManager::Get().SetCurrentOverlayID( std::min(current_overlay_old, OverlayManager::Get().GetOverlayCount() - 1) );
}

void ConfigManager::SaveMultiOverlayProfile(Ini& config)
{
    //Remove single overlay section in case it still exists
    config.RemoveSection("Overlay");

    unsigned int overlay_id = k_ulOverlayID_Dashboard;
    std::stringstream ss;
    ss << "Overlay" << overlay_id;

    //Remove all sequential overlay sections that exist first
    while (config.SectionExists(ss.str().c_str()))
    {
        config.RemoveSection(ss.str().c_str());

        overlay_id++;

        ss = std::stringstream();
        ss << "Overlay" << overlay_id;
    }

    unsigned int current_overlay_old = OverlayManager::Get().GetCurrentOverlayID();

    //Save all overlays in separate sections
    for (unsigned int i = k_ulOverlayID_Dashboard; i < OverlayManager::Get().GetOverlayCount(); ++i)
    {
        OverlayManager::Get().SetCurrentOverlayID(i);

        SaveOverlayProfile(config, i);
    }

    OverlayManager::Get().SetCurrentOverlayID(current_overlay_old);
}

bool ConfigManager::IsUIAccessEnabled()
{
    std::ifstream file_manifest("DesktopPlus.exe.manifest");

    if (file_manifest.good())
    {
        //Read lines and see if 'uiAccess="true"' can be found, otherwise assume it's not enabled
        std::string line_str;

        while (file_manifest.good())
        {
            std::getline(file_manifest, line_str);

            if (line_str.find("uiAccess=\"true\"") != std::string::npos)
            {
                return true;
            }
        }
    }

    return false;
}

void ConfigManager::RemoveScaleFromTransform(Matrix4& transform, float* width)
{
    Vector3 row_1(transform[0], transform[1], transform[2]);
    float scale_x = row_1.length(); //Scaling is always uniform so we just check the x-axis

    if (scale_x == 0.0f)
        return;

    Vector3 translation = transform.getTranslation();
    transform.setTranslation({0.0f, 0.0f, 0.0f});

    transform.scale(1.0f / scale_x);

    transform.setTranslation(translation);

    //Correct the width value so it gives the same visual result as before
    if (width != nullptr)
        *width *= scale_x;
}

void ConfigManager::SaveConfigToFile()
{
    std::wstring wpath = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/config.ini").c_str() );

    //Save to "config_legacy.ini" instead if it exists (see LoadConfigFromFile())
    std::wstring wpath_legacy = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/config_legacy.ini").c_str() );
    if (FileExists(wpath_legacy.c_str()))
    {
        wpath = wpath_legacy;
    }

    Ini config(wpath.c_str());

    SaveMultiOverlayProfile(config);

    config.WriteInt( "Interface", "OverlayCurrentID",         m_ConfigInt[configid_int_interface_overlay_current_id]);
    config.WriteInt( "Interface", "DesktopButtonCyclingMode", m_ConfigInt[configid_int_interface_mainbar_desktop_listing]);
    config.WriteBool("Interface", "DisplaySizeLarge",         m_ConfigBool[configid_bool_interface_large_style]);
    config.WriteBool("Interface", "DesktopButtonIncludeAll",  m_ConfigBool[configid_bool_interface_mainbar_desktop_include_all]);

    //Write color string
    std::stringstream ss;
    ss << std::setw(8) << std::setfill('0') << std::hex << *(unsigned int*)&m_ConfigInt[configid_int_interface_background_color];
    config.WriteString("Interface", "EnvironmentBackgroundColor", ss.str().c_str());

    config.WriteInt( "Interface", "EnvironmentBackgroundColorDisplayMode", m_ConfigInt[configid_int_interface_background_color_display_mode]);
    config.WriteBool("Interface", "DimUI",                                 m_ConfigBool[configid_bool_interface_dim_ui]);
    config.WriteInt( "Interface", "LastVRUIScale",                     int(m_ConfigFloat[configid_float_interface_last_vr_ui_scale] * 100.0f));
    config.WriteBool("Interface", "WarningCompositorResolutionHidden",     m_ConfigBool[configid_bool_interface_warning_compositor_res_hidden]);
    config.WriteBool("Interface", "WarningCompositorQualityHidden",        m_ConfigBool[configid_bool_interface_warning_compositor_quality_hidden]);
    config.WriteBool("Interface", "WarningProcessElevationHidden",         m_ConfigBool[configid_bool_interface_warning_process_elevation_hidden]);
    config.WriteBool("Interface", "WarningElevatedModeHidden",             m_ConfigBool[configid_bool_interface_warning_elevated_mode_hidden]);
    config.WriteBool("Interface", "WarningWelcomeHidden",                  m_ConfigBool[configid_bool_interface_warning_welcome_hidden]);

    //Only write WMR settings when they're not -1 since they get set to that when using a non-WMR system. We want to preserve them for HMD-switching users
    if (m_ConfigInt[configid_int_interface_wmr_ignore_vscreens] != -1)
        config.WriteInt("Interface", "WMRIgnoreVScreens", m_ConfigInt[configid_int_interface_wmr_ignore_vscreens]);

    //Save action order
    ss = std::stringstream();

    for (auto& data : m_ActionManager.GetActionMainBarOrder())
    {
        ss << data.action_id << ' ' << data.visible << ";";
    }

    config.WriteString("Interface", "ActionOrder", ss.str().c_str());

    config.WriteInt( "Input",  "GoHomeButtonActionID",               m_ConfigInt[configid_int_input_go_home_action_id]);
    config.WriteInt( "Input",  "GoBackButtonActionID",               m_ConfigInt[configid_int_input_go_back_action_id]);
    config.WriteInt( "Input",  "GlobalShortcut01ActionID",           m_ConfigInt[configid_int_input_shortcut01_action_id]);
    config.WriteInt( "Input",  "GlobalShortcut02ActionID",           m_ConfigInt[configid_int_input_shortcut02_action_id]);
    config.WriteInt( "Input",  "GlobalShortcut03ActionID",           m_ConfigInt[configid_int_input_shortcut03_action_id]);

    config.WriteInt( "Input",  "GlobalHotkey01Modifiers",            m_ConfigInt[configid_int_input_hotkey01_modifiers]);
    config.WriteInt( "Input",  "GlobalHotkey01KeyCode",              m_ConfigInt[configid_int_input_hotkey01_keycode]);
    config.WriteInt( "Input",  "GlobalHotkey01ActionID",             m_ConfigInt[configid_int_input_hotkey01_action_id]);
    config.WriteInt( "Input",  "GlobalHotkey02Modifiers",            m_ConfigInt[configid_int_input_hotkey02_modifiers]);
    config.WriteInt( "Input",  "GlobalHotkey02KeyCode",              m_ConfigInt[configid_int_input_hotkey02_keycode]);
    config.WriteInt( "Input",  "GlobalHotkey02ActionID",             m_ConfigInt[configid_int_input_hotkey02_action_id]);
    config.WriteInt( "Input",  "GlobalHotkey03Modifiers",            m_ConfigInt[configid_int_input_hotkey03_modifiers]);
    config.WriteInt( "Input",  "GlobalHotkey03KeyCode",              m_ConfigInt[configid_int_input_hotkey03_keycode]);
    config.WriteInt( "Input",  "GlobalHotkey03ActionID",             m_ConfigInt[configid_int_input_hotkey03_action_id]);

    config.WriteInt( "Input",  "DetachedInteractionMaxDistance", int(m_ConfigFloat[configid_float_input_detached_interaction_max_distance] * 100.0f));
    config.WriteBool("Input",  "GlobalHMDPointer",                   m_ConfigBool[configid_bool_input_global_hmd_pointer]);
    config.WriteInt( "Input",  "GlobalHMDPointerMaxDistance",    int(m_ConfigFloat[configid_float_input_global_hmd_pointer_max_distance] * 100.0f));

    config.WriteBool("Mouse", "RenderCursor",              m_ConfigBool[configid_bool_input_mouse_render_cursor]);
    config.WriteBool("Mouse", "RenderIntersectionBlob",    m_ConfigBool[configid_bool_input_mouse_render_intersection_blob]);
    config.WriteBool("Mouse", "HMDPointerOverride",        m_ConfigBool[configid_bool_input_mouse_hmd_pointer_override]);
    config.WriteInt( "Mouse", "DoubleClickAssistDuration", m_ConfigInt[configid_int_input_mouse_dbl_click_assist_duration_ms]);

    config.WriteBool("Keyboard", "EnableKeyboardHelper",        m_ConfigBool[configid_bool_input_keyboard_helper_enabled]);
    config.WriteInt( "Keyboard", "KeyboardDetachedSize",    int(m_ConfigFloat[configid_float_input_keyboard_detached_size] * 100.0f));

    config.WriteBool("Windows", "AutoFocusSceneAppDashboard",   m_ConfigBool[configid_bool_windows_auto_focus_scene_app_dashboard]);
    config.WriteBool("Windows", "WinRTAutoFocus",               m_ConfigBool[configid_bool_windows_winrt_auto_focus]);
    config.WriteBool("Windows", "WinRTKeepOnScreen",            m_ConfigBool[configid_bool_windows_winrt_keep_on_screen]);
    config.WriteInt( "Windows", "WinRTDraggingMode",            m_ConfigInt[configid_int_windows_winrt_dragging_mode]);
    config.WriteBool("Windows", "WinRTAutoSizeOverlay",         m_ConfigBool[configid_bool_windows_winrt_auto_size_overlay]);
    config.WriteBool("Windows", "WinRTAutoFocusSceneApp",       m_ConfigBool[configid_bool_windows_winrt_auto_focus_scene_app]);
    
    config.WriteInt( "Performance", "UpdateLimitMode",                      m_ConfigInt[configid_int_performance_update_limit_mode]);
    config.WriteInt( "Performance", "UpdateLimitMS",                    int(m_ConfigFloat[configid_float_performance_update_limit_ms] * 100.0f));
    config.WriteInt( "Performance", "UpdateLimitFPS",                       m_ConfigInt[configid_int_performance_update_limit_fps]);
    config.WriteBool("Performance", "RapidLaserPointerUpdates",             m_ConfigBool[configid_bool_performance_rapid_laser_pointer_updates]);
    config.WriteBool("Performance", "SingleDesktopMirroring",               m_ConfigBool[configid_bool_performance_single_desktop_mirroring]);
    config.WriteBool("Performance", "PerformanceMonitorStyleLarge",         m_ConfigBool[configid_bool_performance_monitor_large_style]);
    config.WriteBool("Performance", "PerformanceMonitorShowGraphs",         m_ConfigBool[configid_bool_performance_monitor_show_graphs]);
    config.WriteBool("Performance", "PerformanceMonitorShowTime",           m_ConfigBool[configid_bool_performance_monitor_show_time]);
    config.WriteBool("Performance", "PerformanceMonitorShowCPU",            m_ConfigBool[configid_bool_performance_monitor_show_cpu]);
    config.WriteBool("Performance", "PerformanceMonitorShowGPU",            m_ConfigBool[configid_bool_performance_monitor_show_gpu]);
    config.WriteBool("Performance", "PerformanceMonitorShowFPS",            m_ConfigBool[configid_bool_performance_monitor_show_fps]);
    config.WriteBool("Performance", "PerformanceMonitorShowBattery",        m_ConfigBool[configid_bool_performance_monitor_show_battery]);
    config.WriteBool("Performance", "PerformanceMonitorShowTrackers",       m_ConfigBool[configid_bool_performance_monitor_show_trackers]);
    config.WriteBool("Performance", "PerformanceMonitorShowViveWireless",   m_ConfigBool[configid_bool_performance_monitor_show_vive_wireless]);
    config.WriteBool("Performance", "PerformanceMonitorDisableGPUCounters", m_ConfigBool[configid_bool_performance_monitor_disable_gpu_counters]);

    config.WriteBool("Misc", "NoSteam",            m_ConfigBool[configid_bool_misc_no_steam]);
    config.WriteBool("Misc", "ApplySteamVR2DashboardOffset",            m_ConfigBool[configid_bool_misc_apply_steamvr2_dashboard_offset]);
    config.WriteBool("Misc", "UIAccessWasEnabled", (m_ConfigBool[configid_bool_misc_uiaccess_was_enabled] || m_ConfigBool[configid_bool_state_misc_uiaccess_enabled]));

    //Save custom actions
    config.RemoveSection("CustomActions"); //Remove old section first to avoid any leftovers

    auto& custom_actions = m_ActionManager.GetCustomActions();
    int custom_action_count = (int)custom_actions.size();
    config.WriteInt("CustomActions", "Count", custom_action_count);

    for (int i = 0; i < custom_action_count; ++i)
    {
        const CustomAction& action = custom_actions[i];
        std::string action_ini_name = "Action" + std::to_string(i);
        
        config.WriteString("CustomActions", (action_ini_name + "Name").c_str(), action.Name.c_str());
        config.WriteString("CustomActions", (action_ini_name + "FunctionType").c_str(), ActionManager::CustomActionFunctionToString(action.FunctionType));

        switch (action.FunctionType)
        {
            case caction_press_keys:
            {
                config.WriteInt( "CustomActions", (action_ini_name + "KeyCode1").c_str(),    action.KeyCodes[0]);
                config.WriteInt( "CustomActions", (action_ini_name + "KeyCode2").c_str(),    action.KeyCodes[1]);
                config.WriteInt( "CustomActions", (action_ini_name + "KeyCode3").c_str(),    action.KeyCodes[2]);
                config.WriteBool("CustomActions", (action_ini_name + "ToggleKeys").c_str(), (action.IntID == 1));
                break;
            }
            case caction_type_string:
            {
                config.WriteString("CustomActions", (action_ini_name + "TypeString").c_str(), action.StrMain.c_str());
                break;
            }
            case caction_launch_application:
            {
                config.WriteString("CustomActions", (action_ini_name + "ExecutablePath").c_str(), action.StrMain.c_str());
                config.WriteString("CustomActions", (action_ini_name + "ExecutableArg").c_str(),  action.StrArg.c_str());
                break;
            }
            case caction_toggle_overlay_enabled_state:
            {
                config.WriteInt("CustomActions", (action_ini_name + "OverlayID").c_str(), action.IntID);
                break;
            }
        }

        #ifdef DPLUS_UI
            config.WriteString("CustomActions", (action_ini_name + "IconFilename").c_str(), action.IconFilename.c_str());
        #endif
    }

    config.Save();
}

void ConfigManager::RestoreConfigFromDefault()
{
    //Basically delete the config file and then load it again which will fall back to config_default.ini
    std::wstring wpath = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/config.ini").c_str() );
    ::DeleteFileW(wpath.c_str());

    LoadConfigFromFile();
}

void ConfigManager::LoadOverlayProfileDefault(bool multi_overlay)
{
    Ini config(L"");

    //Multi-Overlay "default" config is removing all overlays except dashboard and defaulting that
    if (multi_overlay)
    {
        OverlayManager::Get().RemoveAllOverlays();

        OverlayManager::Get().GetConfigData(k_ulOverlayID_Dashboard).ConfigNameStr = ""; //Have the dashboard name reset on LoadOverlayProfile()
    }
    else if (OverlayManager::Get().GetCurrentOverlayID() != k_ulOverlayID_Dashboard)
    {
        //Adjust width here as the zero matrices will not get unscaled by LoadOverlayProfile() and yet it'll go with the old default width which is now too big
        config.WriteInt("Overlay", "Width", 165);
    }

    LoadOverlayProfile(config); //All read calls will fail end fill in default values as a result
}

bool ConfigManager::LoadOverlayProfileFromFile(const std::string filename)
{
    std::wstring wpath = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/profiles/overlays/" + filename).c_str() );

    if (FileExists(wpath.c_str()))
    {
        Ini config(wpath);
        LoadOverlayProfile(config);
        return true;
    }

    return false;
}

void ConfigManager::SaveOverlayProfileToFile(const std::string filename)
{
    std::string path = m_ApplicationPath + "/profiles/overlays/" + filename;
    Ini config(WStringConvertFromUTF8(path.c_str()));

    SaveOverlayProfile(config);
    config.Save();
}

bool ConfigManager::LoadMultiOverlayProfileFromFile(const std::string filename, bool clear_existing_overlays)
{
    std::wstring wpath = WStringConvertFromUTF8( std::string(m_ApplicationPath + "/profiles/multi-overlays/" + filename).c_str() );

    if (FileExists(wpath.c_str()))
    {
        Ini config(wpath);
        LoadMultiOverlayProfile(config, clear_existing_overlays);
        return true;
    }

    return false;
}

void ConfigManager::SaveMultiOverlayProfileToFile(const std::string filename)
{
    std::string path = m_ApplicationPath + "/profiles/multi-overlays/" + filename;
    Ini config(WStringConvertFromUTF8(path.c_str()));

    SaveMultiOverlayProfile(config);
    config.Save();
}

bool ConfigManager::DeleteOverlayProfile(const std::string filename, bool multi_overlay)
{
    std::string path = m_ApplicationPath + "profiles/" + ((multi_overlay) ? "multi-overlays/" : "overlays/") + filename;
    return (::DeleteFileW(WStringConvertFromUTF8(path.c_str()).c_str()) != 0);
}

std::vector<std::string> ConfigManager::GetOverlayProfileList(bool multi_overlay)
{
    std::vector<std::string> list;
    list.emplace_back("Default");

    const std::wstring wpath = WStringConvertFromUTF8(std::string(m_ApplicationPath + "profiles/" + ((multi_overlay) ? "multi-overlays" : "overlays") + "/*.ini").c_str());
    WIN32_FIND_DATA find_data;
    HANDLE handle_find = ::FindFirstFileW(wpath.c_str(), &find_data);

    if (handle_find != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string name = StringConvertFromUTF16(find_data.cFileName);
            name = name.substr(0, name.length() - 4);   //Remove extension

            list.push_back(name);
        }
        while (::FindNextFileW(handle_find, &find_data) != 0);

        ::FindClose(handle_find);
    }

    list.emplace_back("[New Profile]");

    return list;
}

WPARAM ConfigManager::GetWParamForConfigID(ConfigID_Bool id)    //This is a no-op, but for consistencies' sake and in case anything changes there, it still exists
{
    return id;
}

WPARAM ConfigManager::GetWParamForConfigID(ConfigID_Int id)
{
    return id + configid_bool_MAX;
}

WPARAM ConfigManager::GetWParamForConfigID(ConfigID_Float id)
{
    return id + configid_bool_MAX + configid_int_MAX;
}

WPARAM ConfigManager::GetWParamForConfigID(ConfigID_IntPtr id)
{
    return id + configid_bool_MAX + configid_int_MAX + configid_float_MAX;
}

void ConfigManager::SetConfigBool(ConfigID_Bool id, bool value)
{
    if (id < configid_bool_overlay_MAX)
        OverlayManager::Get().GetCurrentConfigData().ConfigBool[id] = value;
    else if (id < configid_bool_MAX)
        m_ConfigBool[id] = value;
}

void ConfigManager::SetConfigInt(ConfigID_Int id, int value)
{
    if (id < configid_int_overlay_MAX)
        OverlayManager::Get().GetCurrentConfigData().ConfigInt[id] = value;
    else if (id < configid_int_MAX)
        m_ConfigInt[id] = value;
}

void ConfigManager::SetConfigFloat(ConfigID_Float id, float value)
{
    if (id < configid_float_overlay_MAX)
        OverlayManager::Get().GetCurrentConfigData().ConfigFloat[id] = value;
    else if (id < configid_float_MAX)
        m_ConfigFloat[id] = value;
}

void ConfigManager::SetConfigIntPtr(ConfigID_IntPtr id, intptr_t value)
{
    OverlayManager::Get().GetCurrentConfigData().ConfigIntPtr[id] = value;
}

void ConfigManager::SetConfigString(ConfigID_String id, const std::string& value)
{
    if (id < configid_str_overlay_MAX)
        OverlayManager::Get().GetCurrentConfigData().ConfigStr[id] = value;
    else if (id < configid_str_MAX)
        m_ConfigString[id] = value;
}

//The GetConfig*() functions assume the caller knows what they're doing and don't shove *_MAX or an unchecked cast in there. For performance
bool ConfigManager::GetConfigBool(ConfigID_Bool id) const
{
    return (id < configid_bool_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigBool[id] : m_ConfigBool[id];
}

int ConfigManager::GetConfigInt(ConfigID_Int id) const
{
    return (id < configid_int_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigInt[id] : m_ConfigInt[id];
}

float ConfigManager::GetConfigFloat(ConfigID_Float id) const
{
    return (id < configid_float_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigFloat[id] : m_ConfigFloat[id];
}

intptr_t ConfigManager::GetConfigIntPtr(ConfigID_IntPtr id) const
{
    return OverlayManager::Get().GetCurrentConfigData().ConfigIntPtr[id];
}

const std::string& ConfigManager::GetConfigString(ConfigID_String id) const
{
    return (id < configid_str_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigStr[id] : m_ConfigString[id];
}

bool& ConfigManager::GetConfigBoolRef(ConfigID_Bool id)
{
    return (id < configid_bool_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigBool[id] : m_ConfigBool[id];
}

int& ConfigManager::GetConfigIntRef(ConfigID_Int id)
{
    return (id < configid_int_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigInt[id] : m_ConfigInt[id];
}

float& ConfigManager::GetConfigFloatRef(ConfigID_Float id)
{
    return (id < configid_float_overlay_MAX) ? OverlayManager::Get().GetCurrentConfigData().ConfigFloat[id] : m_ConfigFloat[id];
}

intptr_t& ConfigManager::GetConfigIntPtrRef(ConfigID_IntPtr id)
{
    return OverlayManager::Get().GetCurrentConfigData().ConfigIntPtr[id];
}

void ConfigManager::ResetConfigStateValues()
{
    std::fill(std::begin(m_ConfigBool) + configid_bool_state_overlay_dragmode,           std::begin(m_ConfigBool) + configid_bool_state_misc_process_started_by_steam, false);
    std::fill(std::begin(m_ConfigInt)  + configid_int_state_overlay_current_id_override, std::begin(m_ConfigInt)  + configid_int_state_performance_duplication_fps,    -1);
    //configid_int_state_interface_desktop_count is not reset
}

ActionManager& ConfigManager::GetActionManager()
{
    return m_ActionManager;
}

std::vector<CustomAction>& ConfigManager::GetCustomActions()
{
    return m_ActionManager.GetCustomActions();
}

std::vector<ActionMainBarOrderData>& ConfigManager::GetActionMainBarOrder()
{
    return m_ActionManager.GetActionMainBarOrder();
}

Matrix4& ConfigManager::GetOverlayDetachedTransform()
{
    int origin = GetConfigInt(configid_int_overlay_detached_origin);

    if (origin < ovrl_origin_MAX)
        return OverlayManager::Get().GetCurrentConfigData().ConfigDetachedTransform[origin];
    else
        return OverlayManager::Get().GetCurrentConfigData().ConfigDetachedTransform[ovrl_origin_room];
}

const std::string& ConfigManager::GetApplicationPath() const
{
    return m_ApplicationPath;
}

const std::string& ConfigManager::GetExecutableName() const
{
	return m_ExecutableName;
}

bool ConfigManager::IsSteamInstall() const
{
    return m_IsSteamInstall;
}
