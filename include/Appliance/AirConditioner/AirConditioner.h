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
  void m_onIdle() override;
  void m_onRequest(const Frame &frame) override;
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

  bool setVerticalLouverPosition(uint8_t position);
  bool setHorizontalLouverPosition(uint8_t position);
  void requestLouverStatus() { this->m_louverQueryPending = true; }

  bool hasVerticalLouverPosition() const { return this->m_verticalLouverKnown; }
  bool hasHorizontalLouverPosition() const { return this->m_horizontalLouverKnown; }

  uint8_t getVerticalLouverPosition() const { return this->m_verticalLouverPosition; }
  uint8_t getHorizontalLouverPosition() const { return this->m_horizontalLouverPosition; }

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

  // Runtime status from normal C0 response
  bool hasDisplayStatus() const { return this->m_displayStatusKnown; }
  bool isDisplayOn() const { return this->m_displayOn; }
  bool hasDisplayStatusRaw() const { return this->m_displayStatusRawKnown; }
  uint8_t getDisplayStatusRaw() const { return this->m_displayStatusRaw; }

  bool hasIndoorFanStatus() const { return this->m_indoorFanKnown; }
  uint8_t getIndoorFanRaw() const { return this->m_indoorFanRaw; }

  bool isIndoorFanAuto() const { return this->m_indoorFanKnown && (this->m_indoorFanRaw == 0x65 || this->m_indoorFanRaw == 0x66); }
  bool hasIndoorFanPercent() const { return this->m_indoorFanKnown && this->m_indoorFanRaw >= 1 && this->m_indoorFanRaw <= 100; }
  float getIndoorFanPercent() const { return static_cast<float>(this->m_indoorFanRaw); }

  bool hasFollowMeStatus() const { return this->m_followMeKnown; }
  bool isFollowMeActive() const { return this->m_followMeKnown && this->m_followMeActive; }
  bool hasFollowMeTemperature() const { return this->m_followMeTemperatureKnown; }
  float getFollowMeTemperature() const { return this->m_followMeTemperature; }

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

  // Runtime status from normal C0 response
  bool m_displayStatusKnown{false};
  bool m_displayOn{false};

  bool m_displayStatusRawKnown{false};
  uint8_t m_displayStatusRaw{0};

  bool m_indoorFanKnown{false};
  uint8_t m_indoorFanRaw{0};

  bool m_followMeKnown{false};
  bool m_followMeActive{false};

  bool m_followMeTemperatureKnown{false};
  float m_followMeTemperature{0.0f};

  bool m_sendLouverCommand();
  bool m_sendLouverStatusQuery();
  void m_readLouverStatus(FrameData data);

  Timer m_louverRefreshTimer;
  Timer m_louverPollTimer;

  bool m_louverCommandPending{false};
  bool m_louverQueryPending{false};

  uint8_t m_pendingVerticalLouverPosition{3};
  uint8_t m_pendingHorizontalLouverPosition{3};
  uint8_t m_louverMessageId{0};

  bool m_verticalLouverKnown{false};
  bool m_horizontalLouverKnown{false};
  uint8_t m_verticalLouverPosition{3};
  uint8_t m_horizontalLouverPosition{3};

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
