//
//  MachOUtils.h
//  Feather
//
//  Created by samara on 12.06.2025.
//

@import Darwin;
@import Foundation;
@import MachO;

NSString *LCPatchMachOFixupARM64eSlice(const char *path);
NSString *LCPatchMachOForSDK26(const char *path);
/// Sets the sdk field of LC_BUILD_VERSION / LC_VERSION_MIN_IPHONEOS in every slice. Returns nil on success.
NSString *LCPatchMachOSDKVersion(const char *path, uint32_t sdk);
NSData *LCGetMachOEntitlements(const char *path);
