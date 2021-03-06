// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2017-2020 Egor Pugin <egor.pugin@gmail.com>

#pragma once

#include "inheritance.h"

#include <sw/core/settings.h>
#include <sw/core/target.h>
#include <sw/manager/package.h>

#include <memory>

namespace sw
{

struct ITarget;

struct SW_DRIVER_CPP_API DependencyData : IDependency
{
    UnresolvedPackage package;
    PackageSettings settings;
    bool Disabled = false;

    DependencyData(const ITarget &t);
    DependencyData(const UnresolvedPackage &p);

    UnresolvedPackage getPackage() const;
    UnresolvedPackage getUnresolvedPackage() const override { return getPackage(); }
    void setTarget(const ITarget &t) override;
    const ITarget &getTarget() const override;

    bool isDisabled() const { return Disabled; }

    operator bool() const { return target; }
    bool isResolved() const override { return operator bool(); }

    PackageId getResolvedPackage() const;

    PackageSetting &getOption(const String &name) { return getOptions()[name]; }
    const PackageSetting &getOption(const String &name) const { return getOptions()[name]; }
    void setOption(const String &name, const PackageSetting &value) { getOption(name) = value; }

    PackageSettings &getOptions() { return getSettings()["options"].getMap(); }
    const PackageSettings &getOptions() const { return getSettings()["options"].getMap(); }

    PackageSettings &getSettings() { return settings; }
    const PackageSettings &getSettings() const override { return settings; }

private:
    const ITarget *target = nullptr;
};

struct SW_DRIVER_CPP_API Dependency : DependencyData
{
    bool GenerateCommandsBefore = false; // do not make true by default
    bool IncludeDirectoriesOnly = false;
    bool LinkLibrariesOnly = false;

    using DependencyData::DependencyData;

    // for backwards compat
    void setDummy(bool) {}
};

using DependencyPtr = std::shared_ptr<Dependency>;

struct SW_DRIVER_CPP_API TargetDependency
{
    DependencyPtr dep;
    InheritanceType inhtype;
};

}

namespace std
{

template<> struct hash<sw::DependencyData>
{
    size_t operator()(const sw::DependencyData &p) const
    {
        return std::hash<decltype(p.package)>()(p.package);
    }
};

template<> struct hash<sw::Dependency>
{
    size_t operator()(const sw::Dependency& p) const
    {
        return std::hash<decltype(p.package)>()(p.package);
    }
};

}
