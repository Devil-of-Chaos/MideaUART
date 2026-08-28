#include "Appliance/AirConditioner/AirConditioner.h"
#include "Appliance/AirConditioner/BoschQueryData.h"
#include "Helpers/Timer.h"
#include "Helpers/Log.h"

namespace dudanov {
namespace midea {
namespace ac {

static const char *TAG = "AirConditioner";

void AirConditioner::m_setup() {
  if (this->m_autoconfStatus != AUTOCONF_DISABLED)
    this->m_getCapabilities();
  this->m_timerManager.registerTimer(this->m_powerUsageTimer);
  this->m_powerUsageTimer.setCallback([this](Timer *timer) {
    timer->reset();
    this->m_getPowerUsage();
  });
  this->m_powerUsageTimer.start(30000);
  this->m_timerManager.registerTimer(this->m_boschExtendedTimer);

  this->m_boschExtendedTimer.setCallback([this](Timer *timer) {
    timer->reset();

    static const uint8_t groups[] = {
      0x41,
      0x42,
      0x44,
      0x45
    };

    this->m_getBoschExtended(
        groups[this->m_boschExtendedGroupIndex]
    );

    this->m_boschExtendedGroupIndex++;

    if (this->m_boschExtendedGroupIndex >=
        sizeof(groups) / sizeof(groups[0])) {
      this->m_boschExtendedGroupIndex = 0;
    }
  });

  this->m_boschExtendedTimer.start(5000);
}

static bool checkConstraints(const Mode &mode, const Preset &preset) {
  if (mode == Mode::MODE_OFF)
    return preset == Preset::PRESET_NONE;
  switch (preset) {
    case Preset::PRESET_NONE:
      return true;
    case Preset::PRESET_ECO:
      return mode == Mode::MODE_COOL;
    case Preset::PRESET_TURBO:
      return mode == Mode::MODE_COOL || mode == Mode::MODE_HEAT;
    case Preset::PRESET_SLEEP:
      return mode != Mode::MODE_DRY && mode != Mode::MODE_FAN_ONLY;
    case Preset::PRESET_FREEZE_PROTECTION:
      return mode == Mode::MODE_HEAT;
    default:
      return false;
  }
}

void AirConditioner::control(const Control &control) {
  if (this->m_sendControl)
    return;
  StatusData status = this->m_status;
  Mode mode = this->m_mode;
  Preset preset = this->m_preset;
  bool hasUpdate = false;
  bool isModeChanged = false;
  if (control.mode.hasUpdate(mode)) {
    hasUpdate = true;
    isModeChanged = true;
    mode = control.mode.value();
    if (this->m_mode == Mode::MODE_OFF)
      preset = this->m_lastPreset;
    else if (!checkConstraints(mode, preset))
      preset = Preset::PRESET_NONE;
  }
  if (control.preset.hasUpdate(preset) && checkConstraints(mode, control.preset.value())) {
    hasUpdate = true;
    preset = control.preset.value();
  }
  if (mode != Mode::MODE_OFF) {
    if (mode == Mode::MODE_AUTO || preset != Preset::PRESET_NONE) {
      if (this->m_fanMode != FanMode::FAN_AUTO) {
        hasUpdate = true;
        status.setFanMode(FanMode::FAN_AUTO);
      }
    } else if (control.fanMode.hasUpdate(this->m_fanMode)) {
      hasUpdate = true;
      status.setFanMode(control.fanMode.value());
    }
    if (control.swingMode.hasUpdate(this->m_swingMode)) {
      hasUpdate = true;
      status.setSwingMode(control.swingMode.value());
    }
  }
  if (control.targetTemp.hasUpdate(this->m_targetTemp)) {
    hasUpdate = true;
    status.setTargetTemp(control.targetTemp.value());
  }
  if (hasUpdate) {
    this->m_sendControl = true;
    status.setMode(mode);
    status.setPreset(preset);
    status.setBeeper(this->m_beeper);
    status.appendCRC();
    if (isModeChanged && preset != Preset::PRESET_NONE && preset != Preset::PRESET_SLEEP) {
      // Last command with preset
      this->m_setStatus(status);
      status.setPreset(Preset::PRESET_NONE);
      status.setBeeper(false);
      status.updateCRC();
      // First command without preset
      this->m_queueRequestPriority(FrameType::DEVICE_CONTROL, std::move(status),
        // onData
        std::bind(&AirConditioner::m_readStatus, this, std::placeholders::_1)
      );
    } else {
      this->m_setStatus(std::move(status));
    }
  }
}

void AirConditioner::m_setStatus(StatusData status) {
  LOG_D(TAG, "Enqueuing a priority SET_STATUS(0x40) request...");
  this->m_queueRequestPriority(FrameType::DEVICE_CONTROL, std::move(status),
    // onData
    std::bind(&AirConditioner::m_readStatus, this, std::placeholders::_1),
    // onSuccess
    [this]() {
      this->m_sendControl = false;
    },
    // onError
    [this]() {
      LOG_W(TAG, "SET_STATUS(0x40) request failed...");
      this->m_sendControl = false;
    }
  );
}

void AirConditioner::setPowerState(bool state) {
  if (state != this->getPowerState()) {
    Control control;
    control.mode = state ? this->m_status.getRawMode() : Mode::MODE_OFF;
    this->control(control);
  }
}

void AirConditioner::m_getPowerUsage() {
  QueryPowerData data{};
  LOG_D(TAG, "Enqueuing a GET_POWERUSAGE(0x41) request...");
  this->m_queueRequest(FrameType::DEVICE_QUERY, std::move(data),
    // onData
    [this](FrameData data) -> ResponseStatus {
      const auto status = data.to<StatusData>();
      if (!status.hasPowerInfo())
        return ResponseStatus::RESPONSE_WRONG;
      if (this->m_powerUsage != status.getPowerUsage()) {
        this->m_powerUsage = status.getPowerUsage();
        this->sendUpdate();
      }
      return ResponseStatus::RESPONSE_OK;
    }
  );
}

void AirConditioner::m_getCapabilities() {
  GetCapabilitiesData data{};
  this->m_autoconfStatus = AUTOCONF_PROGRESS;
  LOG_D(TAG, "Enqueuing a priority GET_CAPABILITIES(0xB5) request...");
  this->m_queueRequest(FrameType::DEVICE_QUERY, std::move(data),
    // onData
    [this](FrameData data) -> ResponseStatus {
      if (!data.hasID(0xB5))
        return ResponseStatus::RESPONSE_WRONG;
      if (this->m_capabilities.read(data)) {
        GetCapabilitiesSecondData data{};
        this->m_sendFrame(FrameType::DEVICE_QUERY, data);
        return ResponseStatus::RESPONSE_PARTIAL;
      }
      return ResponseStatus::RESPONSE_OK;
    },
    // onSuccess
    [this]() {
      this->m_autoconfStatus = AUTOCONF_OK;
    },
    // onError
    [this]() {
      LOG_W(TAG, "Failed to get 0xB5 capabilities report.");
      this->m_autoconfStatus = AUTOCONF_ERROR;
    }
  );
}

void AirConditioner::m_getStatus() {
  QueryStateData data{};
  LOG_D(TAG, "Enqueuing a GET_STATUS(0x41) request...");
  this->m_queueRequest(FrameType::DEVICE_QUERY, std::move(data),
    // onData
    std::bind(&AirConditioner::m_readStatus, this, std::placeholders::_1)
  );
}

void AirConditioner::m_displayToggle() {
  DisplayToggleData data{};
  LOG_D(TAG, "Enqueuing a priority TOGGLE_LIGHT(0x41) request...");
  this->m_queueRequest(FrameType::DEVICE_QUERY, std::move(data),
    // onData
    std::bind(&AirConditioner::m_readStatus, this, std::placeholders::_1)
  );
}

template<typename T>
void setProperty(T &property, const T &value, bool &update) {
  if (property != value) {
    property = value;
    update = true;
  }
}

ResponseStatus AirConditioner::m_readStatus(FrameData data) {
  if (!data.hasStatus())
    return ResponseStatus::RESPONSE_WRONG;
  LOG_D(TAG, "New status data received. Parsing...");
  bool hasUpdate = false;
  const StatusData newStatus = data.to<StatusData>();
  this->m_status.copyStatus(newStatus);
  if (this->m_mode != newStatus.getMode()) {
    hasUpdate = true;
    this->m_mode = newStatus.getMode();
    if (newStatus.getMode() == Mode::MODE_OFF)
      this->m_lastPreset = this->m_preset;
  }
  setProperty(this->m_preset, newStatus.getPreset(), hasUpdate);
  setProperty(this->m_fanMode, newStatus.getFanMode(), hasUpdate);
  setProperty(this->m_swingMode, newStatus.getSwingMode(), hasUpdate);
  setProperty(this->m_targetTemp, newStatus.getTargetTemp(), hasUpdate);
  setProperty(this->m_indoorTemp, newStatus.getIndoorTemp(), hasUpdate);
  setProperty(this->m_outdoorTemp, newStatus.getOutdoorTemp(), hasUpdate);
  setProperty(this->m_indoorHumidity, newStatus.getHumiditySetpoint(), hasUpdate);
  if (hasUpdate)
    this->sendUpdate();
  return ResponseStatus::RESPONSE_OK;
}

ResponseStatus AirConditioner::m_readBoschExtended(FrameData data) {
  /*
   * Erwartete Antwort:
   *
   * C1 21 01 <group> ...
   */

  if (data.size() < 5)
    return ResponseStatus::RESPONSE_WRONG;

  const uint8_t *raw = data.data();

  if (raw[0] != 0xC1 ||
      raw[1] != 0x21 ||
      raw[2] != 0x01)
    return ResponseStatus::RESPONSE_WRONG;

  const uint8_t group = raw[3];

  bool changed = false;

  switch (group) {
    case 0x41: {
      /*
       * Kältebringer-Mapping:
       *
       * C1 frame[14] -> compressor operating
       * C1 frame[15] -> compressor demand
       * C1 frame[21] -> indoor coil
       * C1 frame[22] -> outdoor coil
       *
       * FrameData beginnt beim Payload-Typ C1.
       * Daher entsprechen die vollständigen
       * Framepositionen 14/15/21/22 hier
       * den Payloadpositionen 4/5/11/12.
       */

      if (data.size() > 5) {
        const uint8_t operating = raw[4];
        const uint8_t demand = raw[5];

        if (!this->m_compressorValuesKnown ||
            operating != this->m_compressorOperatingRaw ||
            demand != this->m_compressorDemandRaw) {

          this->m_compressorOperatingRaw = operating;
          this->m_compressorDemandRaw = demand;
          this->m_compressorValuesKnown = true;

          changed = true;
        }
      }

      if (data.size() > 12) {
        /*
         * Temperaturkodierung:
         * halbe Grad mit Offset 50.
         */

        const float indoor =
            (static_cast<float>(raw[11]) - 50.0f) / 2.0f;

        const float outdoor =
            (static_cast<float>(raw[12]) - 50.0f) / 2.0f;

        if (!this->m_indoorCoilKnown ||
            indoor != this->m_indoorCoilTemp) {

          this->m_indoorCoilTemp = indoor;
          this->m_indoorCoilKnown = true;
          changed = true;
        }

        if (!this->m_outdoorCoilKnown ||
            outdoor != this->m_outdoorCoilTemp) {

          this->m_outdoorCoilTemp = outdoor;
          this->m_outdoorCoilKnown = true;
          changed = true;
        }
      }

      break;
    }

    case 0x42: {
      /*
       * vollständiger Frame[22]
       * -> Payload raw[12]
       */

      if (data.size() > 12) {
        const uint8_t swing = raw[12];

        if (!this->m_boschSwingKnown ||
            swing != this->m_boschSwingRaw) {

          this->m_boschSwingRaw = swing;
          this->m_boschSwingKnown = true;

          changed = true;
        }
      }

      break;
    }

    case 0x44:
      /*
       * Die Bosch beantwortet diese Gruppe.
       * Noch kein zusätzlich ausgewertetes Feld.
       */
      break;

    case 0x45: {
      /*
       * vollständiger Frame[18]
       * -> Payload raw[8]
       */

      if (data.size() > 8) {
        const uint8_t fan = raw[8];

        if (!this->m_outdoorFanKnown ||
            fan != this->m_outdoorFanRaw) {

          this->m_outdoorFanRaw = fan;
          this->m_outdoorFanKnown = true;

          changed = true;
        }
      }

      break;
    }

    default:
      return ResponseStatus::RESPONSE_WRONG;
  }

  this->m_boschExtendedValid = true;

  if (changed)
    this->sendUpdate();

  return ResponseStatus::RESPONSE_OK;
}

}  // namespace ac
}  // namespace midea
}  // namespace dudanov
