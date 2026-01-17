#import "PDBatteryManager.h"

/* 
 * These functions are assumed to be provided by
 * Pocket Darwin's power / IOKit bridge layer.
 * They are C on purpose.
 */
extern int pd_power_has_battery(void);
extern int pd_power_is_charging(void);
extern int pd_power_is_full(void);
extern int pd_power_capacity_percent(void);
extern int pd_power_voltage_mv(void);
extern int pd_power_temperature_tenths(void);
extern int pd_power_time_remaining_minutes(void);

@implementation PDBatteryManager {
    BOOL _hasBattery;
    PDBatteryState _state;
    float _level;
    NSInteger _voltage;
    NSInteger _temperature;
    NSInteger _timeRemaining;
}

+ (instancetype)sharedManager {
    static PDBatteryManager *shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[PDBatteryManager alloc] init];
        [shared updateBatteryInfo];
    });
    return shared;
}

- (BOOL)hasBattery {
    return _hasBattery;
}

- (PDBatteryState)batteryState {
    return _state;
}

- (float)batteryLevel {
    return _level;
}

- (NSInteger)batteryVoltage {
    return _voltage;
}

- (NSInteger)batteryTemperature {
    return _temperature;
}

- (NSInteger)estimatedTimeRemaining {
    return _timeRemaining;
}

- (void)updateBatteryInfo {
    if (!pd_power_has_battery()) {
        _hasBattery = NO;
        _state = PDBatteryStateUnknown;
        _level = -1.0f;
        _voltage = 0;
        _temperature = 0;
        _timeRemaining = -1;
        return;
    }

    _hasBattery = YES;

    if (pd_power_is_full()) {
        _state = PDBatteryStateFull;
    } else if (pd_power_is_charging()) {
        _state = PDBatteryStateCharging;
    } else {
        _state = PDBatteryStateUnplugged;
    }

    int cap = pd_power_capacity_percent();
    _level = (cap >= 0) ? (cap / 100.0f) : -1.0f;

    _voltage = pd_power_voltage_mv();
    _temperature = pd_power_temperature_tenths();
    _timeRemaining = pd_power_time_remaining_minutes();
}

@end
