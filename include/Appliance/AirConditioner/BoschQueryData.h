#pragma once

#include "Frame/FrameData.h"

namespace dudanov {
namespace midea {
namespace ac {

class BoschQueryData : public FrameData {
 public:
  explicit BoschQueryData(uint8_t group)
      : FrameData({
          0x41,
          0x21,
          0x01,
          group,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00,
          0x00
        }) {
    this->appendCRC();
  }
};

}  // namespace ac
}  // namespace midea
}  // namespace dudanov