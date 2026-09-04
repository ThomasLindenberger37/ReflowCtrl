#ifndef REFLOWCTRL_DIGITAL_IO_HPP
#define REFLOWCTRL_DIGITAL_IO_HPP

namespace reflowCtrl {

class DigitalOutput {
   public:
    virtual ~DigitalOutput() = default;
    virtual void set(bool is_on) noexcept = 0;
};

class DigitalInput {
   public:
    virtual ~DigitalInput() = default;
    [[nodiscard]] virtual bool is_active() const noexcept = 0;
};

}  // namespace reflowCtrl

#endif  // REFLOWCTRL_DIGITAL_IO_HPP
