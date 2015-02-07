


#ifndef CallIdentifier_h
#define CallIdentifier_h

#include <runtime/UString.h>
#include "FastAllocBase.h"

namespace JSC {

    struct CallIdentifier : public FastAllocBase {
        UString m_name;
        UString m_url;
        unsigned m_lineNumber;

        CallIdentifier()
            : m_lineNumber(0)
        {
        }

        CallIdentifier(const UString& name, const UString& url, int lineNumber)
            : m_name(name)
            , m_url(url)
            , m_lineNumber(lineNumber)
        {
        }

        inline bool operator==(const CallIdentifier& ci) const { return ci.m_lineNumber == m_lineNumber && ci.m_name == m_name && ci.m_url == m_url; }
        inline bool operator!=(const CallIdentifier& ci) const { return !(*this == ci); }

        struct Hash {
            static unsigned hash(const CallIdentifier& key)
            {
                unsigned hashCodes[3] = {
                    key.m_name.rep()->hash(),
                    key.m_url.rep()->hash(),
                    key.m_lineNumber
                };
                return UString::Rep::computeHash(reinterpret_cast<char*>(hashCodes), sizeof(hashCodes));
            }

            static bool equal(const CallIdentifier& a, const CallIdentifier& b) { return a == b; }
            static const bool safeToCompareToEmptyOrDeleted = true;
        };

        unsigned hash() const { return Hash::hash(*this); }

#ifndef NDEBUG
        operator const char*() const { return c_str(); }
        const char* c_str() const { return m_name.UTF8String().c_str(); }
#endif
    };

} // namespace JSC

namespace WTF {

    template<> struct DefaultHash<JSC::CallIdentifier> { typedef JSC::CallIdentifier::Hash Hash; };

    template<> struct HashTraits<JSC::CallIdentifier> : GenericHashTraits<JSC::CallIdentifier> {
        static void constructDeletedValue(JSC::CallIdentifier& slot)
        {
            new (&slot) JSC::CallIdentifier(JSC::UString(), JSC::UString(), std::numeric_limits<unsigned>::max());
        }
        static bool isDeletedValue(const JSC::CallIdentifier& value)
        {
            return value.m_name.isNull() && value.m_url.isNull() && value.m_lineNumber == std::numeric_limits<unsigned>::max();
        }
    };

} // namespace WTF

#endif  // CallIdentifier_h
