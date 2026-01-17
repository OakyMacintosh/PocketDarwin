#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, PDBatteryState) {
    PDBatteryStateUnknown = 0,
    PDBatteryStateUnplugged,
    PDBatteryStateCharging,
    PDBatteryStateFull
};

@interface PDBatteryManager : NSObject

+ (instancetype)sharedManager;

/* Battery availability */
- (BOOL)hasBattery;

/* Battery state */
- (PDBatteryState)batteryState;

/* Battery level (0.0 – 1.0), or -1.0 if unknown */
- (float)batteryLevel;

/* Voltage in millivolts, or 0 if unknown */
- (NSInteger)batteryVoltage;

/* Temperature in tenths of °C, or 0 if unknown */
- (NSInteger)batteryTemperature;

/* Estimated time remaining in minutes, or -1 if unknown */
- (NSInteger)estimatedTimeRemaining;

/* Force refresh from kernel / HAL */
- (void)updateBatteryInfo;

@end
