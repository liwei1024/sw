// Copyright (C) 2016-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "version.h"

#include <iomanip>
#include <regex>
#include <sstream>

/*int64_t Version::toNumber(int digits) const
{
    if (!Branch.empty())
        return 0;

    if (digits != 2 && digits != 4)
        throw SW_RUNTIME_ERROR("digits must be 2 or 4");

    auto shift = 4 * digits;

    // hex
    int64_t r = 0;
    r |= (int64_t)Major << shift;
    r |= (int64_t)Minor << shift;
    r |= (int64_t)Patch << shift;

    // decimal
    int64_t m = 1;
    int64_t r = 0;
    r += (int64_t)Patch * m;
    for (int i = 0; i < digits; i++)
        m *= 10;
    r += (int64_t)Minor * m;
    for (int i = 0; i < digits; i++)
        m *= 10;
    r += (int64_t)Major * m;

    return r;
}*/

namespace sw
{

std::optional<Version> VersionRange::getMinSatisfyingVersion(const VersionSet &s) const
{
    // add policies?

    if (!s.empty_releases())
    {
        for (auto &v : s.releases())
        {
            if (hasVersion(v))
                return v;
        }
    }

    return Base::getMinSatisfyingVersion(s);
}

std::optional<Version> VersionRange::getMaxSatisfyingVersion(const VersionSet &s) const
{
    // add policies?

    if (!s.empty_releases())
    {
        for (auto i = s.rbegin_releases(); i != s.rend_releases(); ++i)
        {
            if (hasVersion(*i))
                return *i;
        }
    }

    return Base::getMaxSatisfyingVersion(s);
}

}
