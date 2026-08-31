#include "Appliance/AirConditioner/AirConditioner.h"
#include "Appliance/AirConditioner/BoschQueryData.h"
#include "Helpers/Timer.h"
#include "Helpers/Log.h"

namespace dudanov {
namespace midea {
namespace ac {

static const char *TAG = "AirConditioner";

static uint8_t louverIndexToRaw(uint8_t index) {
  switch (index) {
    case 1: return 1;
    case 2: return 25;
    case 3: return 50;
    case 4: return 75;
    case 5: return 100;
    default: return 0;
  }
}

static uint8_t louverRawToIndex(uint8_t raw) {
  switch (raw) {
    case 1: return 1;
    case 25: return 2;
    case 50: return 3;
    case 75: return 4;
    case 100: return 5;
    default: return 0;
  }
}

static uint8_t crc8Maxim(const uint8_t *data, uint8_t size) {
  uint8_t crc = 0;

  for (uint8_t i = 0; i < size; i++) {
    uint8_t value = data[i];

    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint8_t mix = (crc ^ value) & 0x01U;
      crc >>= 1U;
      if (mix != 0)
        crc ^= 0x8CU;
      value >>= 1U;
    }
  }

  return crc;
}

static uint8_t frameChecksum(const uint8_t *data, uint8_t sizeWithoutChecksum) {
  uint8_t sum = 0;

  for (uint8_t i = 1; i < sizeWithoutChecksum; i++)
    sum = static_cast<uint8_t>(sum + data[i]);

  return static_cast<uint8_t>(0U - sum);
}

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
    this->m_timerManager.registerTimer(this->m_louverRefreshTimer);
  this->m_louverRefreshTimer.setCallback([this](Timer *timer) {
    timer->stop();
    this->m_louverQueryPending = true;
  });

  this->m_timerManager.registerTimer(this->m_louverPollTimer);
  this->m_louverPollTimer.setCallback([this](Timer *timer) {
    this->m_louverQueryPending = true;
    timer->reset();
  });
  this->m_louverPollTimer.start(60000);

  // Einmal beim Start die echte Stellung lesen.
  this->m_louverQueryPending = true;
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

void AirConditioner::m_onIdle() {
  if (this->m_louverCommandPending) {
    this->m_louverCommandPending = false;

    if (this->m_sendLouverCommand()) {
      // Das Innengerät kurz arbeiten lassen, danach B1-Rückmeldung lesen.
      this->m_louverRefreshTimer.start(1500);
    } else {
      LOG_W(TAG, "Louver command could not be sent.");
    }
    return;
  }

  if (this->m_louverQueryPending) {
    this->m_louverQueryPending = false;

    if (!this->m_sendLouverStatusQuery())
      LOG_W(TAG, "Louver status query could not be sent.");

    return;
  }

  this->m_getStatus();
}

void AirConditioner::m_onRequest(const Frame &frame) {
  if (!frame.hasType(DEVICE_QUERY))
    return;

  FrameData data = frame.getData();
  if (data.size() == 0 || !data.hasID(0xB1))
    return;

  this->m_readLouverStatus(data);
}

bool AirConditioner::setVerticalLouverPosition(uint8_t position) {
  if (position < 1 || position > 5)
    return false;

  // B0 setzt immer beide Achsen: erst die Iststellung lesen, nichts blind zentrieren.
  if (!this->m_verticalLouverKnown || !this->m_horizontalLouverKnown) {
    this->m_louverQueryPending = true;
    LOG_W(TAG, "Louver positions are not known yet; retry in a few seconds.");
    return false;
  }

  this->m_pendingVerticalLouverPosition = position;
  this->m_pendingHorizontalLouverPosition = this->m_horizontalLouverPosition;
  this->m_louverCommandPending = true;
  return true;
}

bool AirConditioner::setHorizontalLouverPosition(uint8_t position) {
  if (position < 1 || position > 5)
    return false;

  if (!this->m_verticalLouverKnown || !this->m_horizontalLouverKnown) {
    this->m_louverQueryPending = true;
    LOG_W(TAG, "Louver positions are not known yet; retry in a few seconds.");
    return false;
  }

  this->m_pendingVerticalLouverPosition = this->m_verticalLouverPosition;
  this->m_pendingHorizontalLouverPosition = position;
  this->m_louverCommandPending = true;
  return true;
}

bool AirConditioner::m_sendLouverCommand() {
  const uint8_t vertical = louverIndexToRaw(this->m_pendingVerticalLouverPosition);
  const uint8_t horizontal = louverIndexToRaw(this->m_pendingHorizontalLouverPosition);

  if (vertical == 0 || horizontal == 0)
    return false;

  uint8_t frame[] = {
    0xAA, 0x16, 0xAC, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x02, 0xB0, 0x02,
    0x09, 0x00, 0x01, vertical,
    0x0A, 0x00, 0x01, horizontal,
    this->m_louverMessageId++, 0x00, 0x00
  };

  frame[21] = crc8Maxim(frame + 10, 11);
  frame[22] = frameChecksum(frame, 22);

  const bool sent = this->m_sendRawFrame(frame, sizeof(frame));

  if (sent) {
    this->m_verticalLouverPosition = this->m_pendingVerticalLouverPosition;
    this->m_horizontalLouverPosition = this->m_pendingHorizontalLouverPosition;
    this->m_verticalLouverKnown = true;
    this->m_horizontalLouverKnown = true;
    this->sendUpdate();
  }

  return sent;
}

bool AirConditioner::m_sendLouverStatusQuery() {
  static const uint8_t query[] = {
    0xAA, 0x13, 0xAC, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x03, 0xB1, 0x03,
    0x09, 0x00,  // vertikale Lamelle
    0x0A, 0x00,  // horizontale Lamelle
    0x15, 0x00,  // echte Innenluftfeuchte
    0xF4, 0x6C   // CRC + Prüfsumme
  };

  return this->m_sendRawFrame(query, sizeof(query));
}

void AirConditioner::m_readLouverStatus(FrameData data) {
  if (data.size() < 3)
    return;

  const uint8_t *raw = data.data();
  const uint8_t count = raw[1];
  const size_t crcIndex = data.size() - 1;
  size_t pos = 2;
  bool changed = false;

  for (uint8_t i = 0; i < count && pos + 4 <= crcIndex; i++) {
    const uint16_t property =
        static_cast<uint16_t>(raw[pos]) |
        (static_cast<uint16_t>(raw[pos + 1]) << 8);

    const uint8_t valueLength = raw[pos + 3];
    const size_t valuePos = pos + 4;

    if (valuePos + valueLength > crcIndex)
      break;

    if (valueLength >= 1) {
      const uint8_t value = raw[valuePos];
    
      LOG_D(TAG, "Bosch B1 property 0x%04X = %u", property, value);
    
      if (property == 0x0015) {
        if (value <= 100 &&
            (!this->m_indoorHumidityKnown ||
             this->m_indoorHumidity != value)) {
          this->m_indoorHumidity = value;
          this->m_indoorHumidityKnown = true;
          changed = true;
        }
      } else {
        const uint8_t position = louverRawToIndex(value);
    
        if (property == 0x0009 && position != 0) {
          if (!this->m_verticalLouverKnown ||
              this->m_verticalLouverPosition != position) {
            this->m_verticalLouverPosition = position;
            this->m_verticalLouverKnown = true;
            changed = true;
          }
        }
    
        if (property == 0x000A && position != 0) {
          if (!this->m_horizontalLouverKnown ||
              this->m_horizontalLouverPosition != position) {
            this->m_horizontalLouverPosition = position;
            this->m_horizontalLouverKnown = true;
            changed = true;
          }
        }
      }
    }

    pos = valuePos + valueLength;
  }

  if (changed)
    this->sendUpdate();
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

void AirConditioner::m_getBoschExtended(uint8_t group) {
  BoschQueryData data(group);

  LOG_D(TAG, "Enqueuing Bosch extended query for group 0x%02X...", group);

  this->m_queueRequest(
      FrameType::DEVICE_QUERY,
      std::move(data),
      std::bind(
          &AirConditioner::m_readBoschExtended,
          this,
          std::placeholders::_1
      )
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
  setProperty(this->m_humiditySetpoint, newStatus.getHumiditySetpoint(), hasUpdate);

  const uint8_t *raw = data.data();

  // ---------------------------------------------------------
  // Indoor fan feedback
  // C0 payload[3] == full frame[13]
  // ---------------------------------------------------------
  if (data.size() > 3) {
    const uint8_t fan = raw[3];
  
    if ((fan >= 1 && fan <= 100) ||
        fan == 0x65 ||
        fan == 0x66) {
  
      if (!this->m_indoorFanKnown ||
          fan != this->m_indoorFanRaw) {
  
        this->m_indoorFanRaw = fan;
        this->m_indoorFanKnown = true;
        hasUpdate = true;
      }
    }
  }

  // ---------------------------------------------------------
  // Follow Me active
  // C0 payload[8] == full frame[18]
  // ---------------------------------------------------------
  if (data.size() > 8) {
    const bool followMe =
        (raw[8] & 0x80U) != 0;
  
    if (!this->m_followMeKnown ||
        followMe != this->m_followMeActive) {
  
      this->m_followMeKnown = true;
      this->m_followMeActive = followMe;
      hasUpdate = true;
    }
  }
  
  // ---------------------------------------------------------
  // Follow Me reported temperature
  // C0 payload[11] == full frame[21]
  // ---------------------------------------------------------
  if (data.size() > 11) {
    const uint8_t tempRaw = raw[11];
  
    if (tempRaw >= 0x32 &&
        tempRaw <= 0x80) {
  
      const float temperature =
          (static_cast<float>(tempRaw) - 50.0f) / 2.0f;
  
      if (!this->m_followMeTemperatureKnown ||
          temperature != this->m_followMeTemperature) {
  
        this->m_followMeTemperature = temperature;
        this->m_followMeTemperatureKnown = true;
        hasUpdate = true;
      }
    }
  }
  
  // LED display state. C0 payload[14] == complete frame[24].
  constexpr uint8_t DISPLAY_STATUS_OFFSET = 14;
  constexpr uint8_t DISPLAY_ON = 0x00;
  constexpr uint8_t DISPLAY_OFF = 0x70;

  if (data.size() > DISPLAY_STATUS_OFFSET) {
    const uint8_t display = raw[DISPLAY_STATUS_OFFSET];
    const bool rawChanged =
        !this->m_displayStatusRawKnown || display != this->m_displayStatusRaw;

    this->m_displayStatusRawKnown = true;
    this->m_displayStatusRaw = display;

    if (display == DISPLAY_ON || display == DISPLAY_OFF) {
      const bool displayOn = display == DISPLAY_ON;

      if (!this->m_displayStatusKnown || displayOn != this->m_displayOn) {
        LOG_D(TAG, "LED display is %s (C0[14] = 0x%02X)",
              displayOn ? "ON" : "OFF", display);
        this->m_displayStatusKnown = true;
        this->m_displayOn = displayOn;
        hasUpdate = true;
      }
    } else if (rawChanged) {
      LOG_D(TAG, "Unrecognised LED display value in C0[14]: 0x%02X", display);
    }
  }

  
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
       * Observed-Mapping:
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
