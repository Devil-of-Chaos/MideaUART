#pragma once
#include "Helpers/Platform.h"
#include "Appliance/ApplianceBase.h"
#include "Appliance/AirConditioner/Capabilities.h"
#include "Appliance/AirConditioner/StatusData.h"
#include "Helpers/Helpers.h"

namespace dudanov {
namespace midea {
namespace ac {

// Air conditioner control command
struct Control {
  Optional<float> targetTemp{};
  Optional<Mode> mode{};
  Optional<Preset> preset{};
  Optional<FanMode> fanMode{};
  Optional<SwingMode> swingMode{};
};

class AirConditioner : public ApplianceBase {
 public:
  AirConditioner() : ApplianceBase(AIR_CONDITIONER) {}
  void m_setup() override;
  void m_onIdle() override { this->m_getStatus(); }
  void control(const Control &control);
  void setPowerState(bool state);
  bool getPowerState() const { return this->m_mode != Mode::MODE_OFF; }
  void togglePowerState() { this->setPowerState(this->m_mode == Mode::MODE_OFF); }
  float getTargetTemp() const { return this->m_targetTemp; }
  float getIndoorTemp() const { return this->m_indoorTemp; }
  float getOutdoorTemp() const { return this->m_outdoorTemp; }
  float getIndoorHum() const { return this->m_indoorHumidity; }
  float getPowerUsage() const { return this->m_powerUsage; }
  Mode getMode() const { return this->m_mode; }
  SwingMode getSwingMode() const { return this->m_swingMode; }
  FanMode getFanMode() const { return this->m_fanMode; }
  Preset getPreset() const { return this->m_preset; }
  const Capabilities &getCapabilities() const { return this->m_capabilities; }
  void displayToggle() { this->m_displayToggle(); }
  // Bosch extended diagnostics
  bool hasBoschExtendedData() const { return this->m_boschExtendedValid; }

  bool hasIndoorCoilTemp() const { return this->m_indoorCoilKnown; }
  float getIndoorCoilTemp() const { return this->m_indoorCoilTemp; }

  bool hasOutdoorCoilTemp() const { return this->m_outdoorCoilKnown; }
  float getOutdoorCoilTemp() const { return this->m_outdoorCoilTemp; }

  bool hasCompressorValues() const { return this->m_compressorValuesKnown; }
  uint8_t getCompressorOperatingRaw() const { return this->m_compressorOperatingRaw; }
  uint8_t getCompressorDemandRaw() const { return this->m_compressorDemandRaw; }

  bool hasOutdoorFan() const { return this->m_outdoorFanKnown; }
  uint8_t getOutdoorFanRaw() const { return this->m_outdoorFanRaw; }

  bool hasBoschSwing() const { return this->m_boschSwingKnown; }
  uint8_t getBoschSwingRaw() const { return this->m_boschSwingRaw; }
 protected:
  void m_getPowerUsage();
  void m_getCapabilities();
  void m_getStatus();
  void m_setStatus(StatusData status);
  void m_displayToggle();
  ResponseStatus m_readStatus(FrameData data);
  void m_getBoschExtended(uint8_t group);
  ResponseStatus m_readBoschExtended(FrameData data);

  Timer m_boschExtendedTimer;
  uint8_t m_boschExtendedGroupIndex{0};

  bool m_boschExtendedValid{false};

  bool m_indoorCoilKnown{false};
  bool m_outdoorCoilKnown{false};
  float m_indoorCoilTemp{};
  float m_outdoorCoilTemp{};

  bool m_compressorValuesKnown{false};
  uint8_t m_compressorOperatingRaw{};
  uint8_t m_compressorDemandRaw{};

  bool m_outdoorFanKnown{false};
  uint8_t m_outdoorFanRaw{};

  bool m_boschSwingKnown{false};
  uint8_t m_boschSwingRaw{};
  Capabilities m_capabilities{};
  Timer m_powerUsageTimer;
  float m_indoorHumidity{};
  float m_indoorTemp{};
  float m_outdoorTemp{};
  float m_targetTemp{};
  float m_powerUsage{};
  Mode m_mode{Mode::MODE_OFF};
  Preset m_preset{Preset::PRESET_NONE};
  FanMode m_fanMode{FanMode::FAN_AUTO};
  SwingMode m_swingMode{SwingMode::SWING_OFF};
  Preset m_lastPreset{Preset::PRESET_NONE};
  StatusData m_status{};
  bool m_sendControl{};
};

}  // namespace ac
}  // namespace midea
}  // namespace dudanov
