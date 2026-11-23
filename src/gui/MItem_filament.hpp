/// @file
#pragma once

#include <array>

#include <i_window_menu_item.hpp>
#include <i18n.h>
#include <gui/menu_item/menu_item_toggle_switch.hpp>
#include <option/has_pressure_advance_calibration.h>

class MI_LOAD : public IWindowMenuItem {
    constexpr static const char *const label = N_("Load Filament");
    constexpr static const char *const warning_loaded = N_("Filament appears to be already loaded, are you sure you want to load it anyway?");

public:
    MI_LOAD();
    virtual void click(IWindowMenu &) override;
};

class MI_UNLOAD : public IWindowMenuItem {
    constexpr static const char *const label = N_("Unload Filament");

public:
    MI_UNLOAD();
    virtual void click(IWindowMenu &) override;
};

class MI_CHANGE : public IWindowMenuItem {
    constexpr static const char *const label = N_("Change Filament");

public:
    MI_CHANGE();
    virtual void click(IWindowMenu &) override;
};

class MI_CHANGEALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Change All");

public:
    MI_CHANGEALL();

protected:
    virtual void click(IWindowMenu &) override;
};

class MI_LOAD_ALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Load All");

public:
    MI_LOAD_ALL();

protected:
    virtual void click(IWindowMenu &) override;
};

class MI_UNLOAD_ALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Unload All");

public:
    MI_UNLOAD_ALL();

protected:
    virtual void click(IWindowMenu &) override;
};

class MI_PURGE : public IWindowMenuItem {
    constexpr static const char *const label = N_("Purge Filament");

public:
    MI_PURGE();
    virtual void click(IWindowMenu &) override;
};

#if HAS_PRESSURE_ADVANCE_CALIBRATION()
/// Shows the saved calibration of the loaded filament next to the label, e.g. "(0.040 @ 275°C)"
class MI_PRESSURE_ADVANCE_CALIBRATION : public IWindowMenuItem {
    constexpr static const char *const label = N_("Calibrate Pressure Advance");

public:
    MI_PRESSURE_ADVANCE_CALIBRATION();
    virtual void click(IWindowMenu &) override;

protected:
    void printExtension(Rect16 extension_rect, Color color_text, Color color_back, ropfn raster_op) const override;

private:
    std::array<char, 24> saved_calibration {};
};

class MI_PRESSURE_ADVANCE_AFTER_LOAD final : public MenuItemToggleSwitch {
public:
    MI_PRESSURE_ADVANCE_AFTER_LOAD();

protected:
    void toggled(Tristate previous_value) override;
};

class MI_PRESSURE_ADVANCE_GCODE_OVERRIDE final : public MenuItemToggleSwitch {
public:
    MI_PRESSURE_ADVANCE_GCODE_OVERRIDE();

protected:
    void toggled(Tristate previous_value) override;
};
#endif

class MI_ASK_COLOR_ON_LOAD final : public MenuItemToggleSwitch {
public:
    MI_ASK_COLOR_ON_LOAD();

protected:
    void toggled([[maybe_unused]] Tristate previous_value) override;
};
