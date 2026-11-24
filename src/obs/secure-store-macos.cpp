#include "obs/secure-store.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace langlive::obsplugin::obs {

namespace {

// QA builds keep their own item, so a QA login never reaches a production
// build on the same computer (and the other way round).
#if LANGLIVE_QA_BUILD
constexpr const char *kService = "com.langlive.obs-plugin.qa";
constexpr const char *kLabel = "LangLive OBS Plugin (QA)";
#else
constexpr const char *kService = "com.langlive.obs-plugin";
constexpr const char *kLabel = "LangLive OBS Plugin";
#endif

// Owns a Core Foundation object.
template<typename T> class CfRef {
public:
	explicit CfRef(T ref = nullptr) : ref_(ref) {}
	~CfRef()
	{
		if (ref_)
			CFRelease(ref_);
	}
	CfRef(const CfRef &) = delete;
	CfRef &operator=(const CfRef &) = delete;
	T get() const { return ref_; }
	T *out() { return &ref_; }

private:
	T ref_;
};

CFStringRef MakeString(const std::string &text)
{
	return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(text.data()),
				       static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
}

// The attributes that identify the item: a generic password of this
// service and name, in the user's (file-based) login keychain. The
// data-protection keychain would need an entitlement in OBS itself.
CFMutableDictionaryRef MakeQuery(const std::string &name)
{
	CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
								 &kCFTypeDictionaryValueCallBacks);
	CfRef<CFStringRef> service(MakeString(kService));
	CfRef<CFStringRef> account(MakeString(name));
	CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(query, kSecAttrService, service.get());
	CFDictionarySetValue(query, kSecAttrAccount, account.get());
	return query;
}

} // namespace

bool SaveSecret(const std::string &name, const std::string &secret)
{
	CfRef<CFMutableDictionaryRef> query(MakeQuery(name));
	CfRef<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(secret.data()),
					   static_cast<CFIndex>(secret.size())));

	CfRef<CFMutableDictionaryRef> update(CFDictionaryCreateMutable(
		kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
	CFDictionarySetValue(update.get(), kSecValueData, data.get());
	OSStatus status = SecItemUpdate(query.get(), update.get());
	if (status != errSecItemNotFound)
		return status == errSecSuccess;

	CfRef<CFStringRef> label(MakeString(kLabel));
	CFDictionarySetValue(query.get(), kSecAttrLabel, label.get());
	CFDictionarySetValue(query.get(), kSecValueData, data.get());
	status = SecItemAdd(query.get(), nullptr);
	return status == errSecSuccess;
}

std::optional<std::string> LoadSecret(const std::string &name)
{
	CfRef<CFMutableDictionaryRef> query(MakeQuery(name));
	CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
	CfRef<CFTypeRef> result;
	if (SecItemCopyMatching(query.get(), result.out()) != errSecSuccess || !result.get() ||
	    CFGetTypeID(result.get()) != CFDataGetTypeID())
		return std::nullopt;
	const auto data = static_cast<CFDataRef>(result.get());
	return std::string(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
			   static_cast<size_t>(CFDataGetLength(data)));
}

bool DeleteSecret(const std::string &name)
{
	CfRef<CFMutableDictionaryRef> query(MakeQuery(name));
	const OSStatus status = SecItemDelete(query.get());
	return status == errSecSuccess || status == errSecItemNotFound;
}

} // namespace langlive::obsplugin::obs
