#import <Foundation/Foundation.h>
#import <IOBluetooth/IOBluetooth.h>

@interface ClassicScanner : NSObject <IOBluetoothDeviceInquiryDelegate>
@end

@implementation ClassicScanner

- (void)deviceInquiryComplete:(IOBluetoothDeviceInquiry *)sender
                        error:(IOReturn)error
                      aborted:(BOOL)aborted {
    NSArray *devices = [sender foundDevices];
    NSLog(@"[Classic] inquiry complete, found %lu device(s)", (unsigned long)devices.count);
    for (IOBluetoothDevice *dev in devices) {
        NSLog(@"[Classic] name='%@' addr=%@ rssi=%ld class=%u paired=%d connected=%d",
              [dev name] ?: @"",
              [dev addressString] ?: @"",
              (long)[dev rawRSSI],
              (unsigned int)[dev classOfDevice],
              [dev isPaired],
              [dev isConnected]);
    }
    CFRunLoopStop(CFRunLoopGetCurrent());
}

- (void)deviceInquiryDeviceFound:(IOBluetoothDeviceInquiry *)sender
                          device:(IOBluetoothDevice *)device {
    NSLog(@"[Classic] live found: name='%@' addr=%@ rssi=%ld",
          [device name] ?: @"",
          [device addressString] ?: @"",
          (long)[device rawRSSI]);
}

@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSLog(@"[Classic] starting 10 s classic Bluetooth inquiry...");
        ClassicScanner *scanner = [[ClassicScanner alloc] init];
        IOBluetoothDeviceInquiry *inquiry = [IOBluetoothDeviceInquiry inquiryWithDelegate:scanner];
        [inquiry setInquiryLength:10];
        [inquiry setUpdateNewDeviceNames:YES];
        [inquiry start];
        CFRunLoopRun();
        [inquiry stop];
    }
    return 0;
}
