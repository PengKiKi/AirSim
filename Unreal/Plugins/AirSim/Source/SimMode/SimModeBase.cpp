#include "SimModeBase.h"
#include "Misc/MessageDialog.h"
#include "Misc/EngineVersion.h"
#include "AirBlueprintLib.h"
#include "Runtime/Launch/Resources/Version.h"
#include "controllers/Settings.hpp"
#include "SimJoyStick/SimJoyStick.h"

ASimModeBase::ASimModeBase()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ASimModeBase::BeginPlay()
{
    Super::BeginPlay();

    //needs to be done before we call base class
    initializeSettings();

    recording_file_.reset(new RecordingFile());
    record_tick_count = 0;
    setupInputBindings();

    UAirBlueprintLib::LogMessage(TEXT("Press F1 to see help"), TEXT(""), LogDebugLevel::Informational);

    readSettings();
}

void ASimModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    recording_file_.release();

    Super::EndPlay(EndPlayReason);
}


void ASimModeBase::initializeSettings()
{
    //TODO: should this be done somewhere else?
    //load settings file if found
    typedef msr::airlib::Settings Settings;
    try {
        FString settings_filename = FString(Settings::getFullPath("settings.json").c_str());
        FString json_fstring;
        bool load_success = false;
        bool file_found = FPaths::FileExists(settings_filename);
        if (file_found) {
            bool read_sucess = FFileHelper::LoadFileToString(json_fstring, * settings_filename);
            if (read_sucess) {
                Settings& settings = Settings::loadJSonString(TCHAR_TO_UTF8(*json_fstring));
                if (settings.isLoadSuccess()) {
                    UAirBlueprintLib::setLogMessagesHidden(! settings.getBool("LogMessagesVisible", true));
                    UAirBlueprintLib::LogMessageString("Loaded settings from ", TCHAR_TO_UTF8(*settings_filename), LogDebugLevel::Informational);
                    load_success = true;
                }
                else
                    UAirBlueprintLib::LogMessageString("Possibly invalid json string in ", TCHAR_TO_UTF8(*settings_filename), LogDebugLevel::Failure);
            }
            else
                UAirBlueprintLib::LogMessageString("Cannot read settings from ", TCHAR_TO_UTF8(*settings_filename), LogDebugLevel::Failure);
        }

        if (!load_success) {
            //create default settings
            Settings& settings = Settings::loadJSonString("{}");
            //write some settings in new file otherwise the string "null" is written if all settigs are empty
            settings.setString("SeeDocsAt", "https://github.com/Microsoft/AirSim/blob/master/docs/settings.md");
            settings.setDouble("SettingdVersion", 1.0);

            if (!file_found) {
                std::string json_content;
                //TODO: there is a crash in Linux due to settings.saveJSonString(). Remove this workaround after we only support Unreal 4.17
                //https://answers.unrealengine.com/questions/664905/unreal-crashes-on-two-lines-of-extremely-simple-st.html
#ifdef _WIN32
                json_content = settings.saveJSonString();
#else
                json_content = "{ \"SettingdVersion\": 1, \"SeeDocsAt\": \"https://github.com/Microsoft/AirSim/blob/master/docs/settings.md\"}";
#endif
                json_fstring = FString(json_content.c_str());
                FFileHelper::SaveStringToFile(json_fstring, * settings_filename);
                UAirBlueprintLib::LogMessageString("Created settings file at ", TCHAR_TO_UTF8(*settings_filename), LogDebugLevel::Informational);
            }
        }
    }
    catch (std::exception& ex) {
        UAirBlueprintLib::LogMessage(FString("Error loading settings from ~/Documents/AirSim/settings.json"), FString(ex.what()), LogDebugLevel::Failure, 30);
    }
}

void ASimModeBase::readSettings()
{
    typedef msr::airlib::Settings Settings;

    Settings& settings = Settings::singleton();

    settings_version_actual = settings.getFloat("SettingdVersion", 0);
    if (settings_version_actual < settings_version_minimum) {
        if ((settings.size() == 1 && 
            ((settings.getString("SeeDocsAt", "") != "") || settings.getString("see_docs_at", "") != ""))
            || (settings.size() == 0)) {
            //no warnings because we have default settings
        }
        else {
            UAirBlueprintLib::LogMessageString("Your settings file is of old version and possibly not compatible!","", LogDebugLevel::Failure);
            UAirBlueprintLib::LogMessageString("Please look at new settings and update your settings.json: ","https://git.io/v9mYY", LogDebugLevel::Failure);
        }
    }

    usage_scenario = settings.getString("UsageScenario", "");
    default_vehicle_config = settings.getString("DefaultVehicleConfig", "SimpleFlight");
   
    enable_rpc = settings.getBool("RpcEnabled", true);
    //by default we spawn server at local endpoint. Do not use 127.0.0.1 as default below
    //because for docker container default is 0.0.0.0 and people get really confused why things
    //don't work
    api_server_address = settings.getString("LocalHostIp", "");
    is_record_ui_visible = settings.getBool("RecordUIVisible", true);

    std::string view_mode_string = settings.getString("ViewMode", "FlyWithMe");
    if (view_mode_string == "FlyWithMe")
        initial_view_mode = ECameraDirectorMode::CAMERA_DIRECTOR_MODE_FLY_WITH_ME;
    else if (view_mode_string == "Fpv")
        initial_view_mode = ECameraDirectorMode::CAMERA_DIRECTOR_MODE_FPV;
    else if (view_mode_string == "Manual")
        initial_view_mode = ECameraDirectorMode::CAMERA_DIRECTOR_MODE_MANUAL;
    else if (view_mode_string == "GroundObserver")
        initial_view_mode = ECameraDirectorMode::CAMERA_DIRECTOR_MODE_GROUND_OBSERVER;

    physics_engine_name = settings.getString("PhysicsEngineName", "FastPhysicsEngine");
    enable_collision_passthrough = settings.getBool("EnableCollisionPassthrogh", false);
    clock_type = settings.getString("ClockType", 
        default_vehicle_config == "SimpleFlight" ? "SteppableClock" : "ScalableClock");

    Settings record_settings;
    if (settings.getChild("Recording", record_settings)) {
        recording_settings.record_on_move = record_settings.getBool("RecordOnMove", recording_settings.record_on_move);
        recording_settings.record_interval = record_settings.getFloat("RecordInterval", recording_settings.record_interval);
    }
    
    UAirBlueprintLib::LogMessage("Default config: ", default_vehicle_config.c_str(), LogDebugLevel::Informational);
}

void ASimModeBase::Tick(float DeltaSeconds)
{
    if (recording_file_->isRecording())
        ++record_tick_count;
    Super::Tick(DeltaSeconds);
}

void ASimModeBase::reset()
{
    //Should be overridden by derived classes
}

AVehiclePawnBase* ASimModeBase::getFpvVehiclePawn()
{
    //Should be overridden by derived classes
    return nullptr;
}


std::string ASimModeBase::getReport()
{
    static const std::string empty_string = std::string();
    //Should be overridden by derived classes
    return empty_string;
}

void ASimModeBase::setupInputBindings()
{
    UAirBlueprintLib::EnableInput(this);
}

bool ASimModeBase::isRecording()
{
    return recording_file_->isRecording();
}

bool ASimModeBase::isRecordUIVisible()
{
    return is_record_ui_visible;
}

ECameraDirectorMode ASimModeBase::getInitialViewMode()
{
    return initial_view_mode;
}

void ASimModeBase::startRecording()
{
    recording_file_->startRecording();
}

bool ASimModeBase::toggleRecording()
{
    if (isRecording())
        stopRecording();
    else
        startRecording();

    return isRecording();
}

void ASimModeBase::stopRecording()
{
    recording_file_->stopRecording();
}

RecordingFile& ASimModeBase::getRecordingFile()
{
    return *recording_file_;
}