// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "build.h"

#include <solution_build.h>
#include <filesystem.h>

#include <optional>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "build");

namespace sw
{

std::optional<path> findConfig(const path &dir)
{
    for (auto &fn : Build::getAvailableFrontendConfigFilenames())
        if (fs::exists(dir / fn))
            return dir / fn;
    return {};
}

std::optional<path> resolveConfig(const path &file_or_dir)
{
    auto f = file_or_dir;
    if (f.empty())
        f = fs::current_path();
    if (!f.is_absolute())
        f = fs::absolute(f);
    if (fs::is_directory(f))
        return findConfig(f);
    return f;
}

std::unique_ptr<Build> load(const path &file_or_dir)
{
    auto f = resolveConfig(file_or_dir);
    if (!f || !Build::isFrontendConfigFilename(f.value()))
    {
        if (f && !Build::isFrontendConfigFilename(f.value()))
            LOG_INFO(logger, "Unknown config, trying in configless mode. Default mode is native (ASM/C/C++)");

        path p = file_or_dir;
        p = fs::absolute(p);

        auto b = std::make_unique<Build>();
        b->Local = true;
        b->SourceDir = p;
        b->load(p, true);
        return b;
    }

    auto b = std::make_unique<Build>();
    b->Local = true;
    b->SourceDir = f.value().parent_path();
    b->load(f.value());

    return b;
}

void build(const path &p)
{
    auto s = load(p);
    s->execute();
}

void build(const Files &files_or_dirs)
{
    if (files_or_dirs.size() == 1)
        return build(*files_or_dirs.begin());

    // proper multibuilds must get commands and create a single execution plan
    throw SW_RUNTIME_ERROR("not implemented");
}

void build(const Strings &packages)
{
    if (std::all_of(packages.begin(), packages.end(), [](const auto &p)
    {
        return path(p).is_absolute() || fs::exists(p);
    }))
    {
        Files files;
        for (auto &p : packages)
            files.insert(p);
        return build(files);
    }

    StringSet p2;
    for (auto &p : packages)
        p2.insert(p);

    auto b = std::make_unique<Build>();
    b->build_packages(p2);
}

void build(const String &s)
{
    // local file or dir is preferable rather than some remote pkg
    if (fs::exists(s))
        return build(path(s));
    return build(Strings{ s });
}

void run(const PackageId &package)
{
    auto b = std::make_unique<Build>();
    b->run_package(package.toString());
}

std::optional<String> read_config(const path &file_or_dir)
{
    auto f = findConfig(file_or_dir);
    if (!f)
        return {};
    return read_file(f.value());
}

static auto fetch1(const path &fn, const FetchOptions &opts)
{
    auto d = fn.parent_path() / SW_BINARY_DIR / "src";

    SourceDirMap srcs_old;
    if (opts.parallel)
    {
        bool pp = true; // postpone once!
        while (1)
        {
            auto b = std::make_unique<Build>();
            b->NamePrefix = opts.name_prefix;
            b->perform_checks = !pp && !opts.dry_run;
            b->DryRun = !pp && opts.dry_run;
            b->PostponeFileResolving = pp;
            b->source_dirs_by_source = srcs_old;
            b->prefix_source_dir = opts.source_dir;
            if (!pp)
                b->fetch_dir = d;
            b->load(fn);

            SourceDirMap srcs;
            for (const auto &[pkg, t] : b->solutions.begin()->getChildren())
            {
                auto s = t->source; // make a copy!
                checkSourceAndVersion(s, pkg.getVersion());
                if (!isValidSourceUrl(s))
                    throw SW_RUNTIME_ERROR("Invalid source: " + print_source(s));
                srcs[s] = d / get_source_hash(s);
            }

            // src_old has correct root dirs
            if (srcs.size() == srcs_old.size())
            {
                if (srcs.size() == 0)
                    throw SW_RUNTIME_ERROR("no sources found");

                // reset
                b->fetch_dir.clear();
                for (auto &s : b->solutions)
                    s.fetch_dir.clear();

                b->fetch_info.sources = srcs_old;

                return b;
            }

            // with this, we only have two iterations
            // This is a limitation, but on the other hand handling of this
            // become too complex for now.
            // For other cases uses non-parallel mode.
            pp = false;

            download(srcs, opts);
            srcs_old = srcs;
        }
    }
    else
    {
        throw SW_RUNTIME_ERROR("not implemented, try to restore (check and enable) this functionality");

        auto b = std::make_unique<Build>();
        b->NamePrefix = opts.name_prefix;
        b->perform_checks = !opts.dry_run;
        b->DryRun = opts.dry_run;
        b->fetch_dir = d;
        b->prefix_source_dir = opts.source_dir;
        b->load(fn);

        // reset
        b->fetch_dir.clear();
        for (auto &s : b->solutions)
            s.fetch_dir.clear();

        b->fetch_info.sources = srcs_old;

        return b;
    }
}

std::unique_ptr<Build> fetch_and_load(const path &file_or_dir, const FetchOptions &opts)
{
    auto f = resolveConfig(file_or_dir);
    if (!f || !Build::isFrontendConfigFilename(f.value()))
        throw SW_RUNTIME_ERROR("no config found");

    auto b = fetch1(f.value(), opts);

    if (opts.parallel)
    {
        for (const auto &[pkg, t] : b->solutions.begin()->getChildren())
        {
            auto s = t->source; // make a copy!
            applyVersionToUrl(s, pkg.version);
            if (opts.apply_version_to_source)
                applyVersionToUrl(t->source, pkg.version);
        }
    }
    b->prepareStep();
    return std::move(b);
}

}