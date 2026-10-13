#include "SecureStore.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace AetherSDR::mobile {

namespace {

NSString* const kService = @"com.aethersdr.mobile";

NSMutableDictionary* baseQuery(const QString& key)
{
    NSMutableDictionary* query = [NSMutableDictionary dictionary];
    query[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
    query[(__bridge id)kSecAttrService] = kService;
    query[(__bridge id)kSecAttrAccount] = key.toNSString();
    return query;
}

} // namespace

bool saveSecret(const QString& key, const QString& value)
{
    removeSecret(key);
    NSMutableDictionary* item = baseQuery(key);
    item[(__bridge id)kSecValueData] = [value.toNSString() dataUsingEncoding:NSUTF8StringEncoding];
    // Readable after the first unlock so a reconnect can run in the background;
    // never synced to other devices.
    item[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
    return SecItemAdd((__bridge CFDictionaryRef)item, nullptr) == errSecSuccess;
}

QString loadSecret(const QString& key)
{
    NSMutableDictionary* query = baseQuery(key);
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &result) != errSecSuccess || !result) {
        return {};
    }
    NSData* data = (__bridge_transfer NSData*)result;
    NSString* string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    return string ? QString::fromNSString(string) : QString();
}

void removeSecret(const QString& key)
{
    SecItemDelete((__bridge CFDictionaryRef)baseQuery(key));
}

bool secureStoreAvailable()
{
    return true;
}

} // namespace AetherSDR::mobile
