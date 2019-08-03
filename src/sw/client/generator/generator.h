/*
 * SW - Build System and Package Manager
 * Copyright (C) 2017-2019 Egor Pugin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <sw/manager/package_path.h>
#include <sw/manager/version.h>

#include <primitives/emitter.h>
#include <primitives/filesystem.h>

namespace sw
{

struct SwBuild;

}

enum class GeneratorType
{
    Batch,
    CMake,
    CompilationDatabase,
    Make,
    NMake,
    Ninja,
    QMake,
    Shell,
    VisualStudio,
    VisualStudioNMake,
    VisualStudioUtility,
    VisualStudioNMakeAndUtility,

    SwExecutionPlan,
};

struct Generator
{
    virtual ~Generator() = default;

    virtual void generate(const sw::SwBuild &) = 0;
    //void generate(const path &file, const Build &b);
    //virtual void createSolutions(Build &b) {}
    //virtual void initSolutions(Build &b) {}

    static std::unique_ptr<Generator> create(const String &s);

    GeneratorType getType() const { return type; }

private:
    GeneratorType type;
};

struct VSGenerator : Generator
{
    sw::Version version;
    String cwd;
    path dir;
    const path projects_dir = "projects";
    const sw::InsecurePath deps_subdir = "Dependencies";
    const sw::InsecurePath overridden_deps_subdir = "Overridden Packages";
    const String predefined_targets_dir = ". SW Predefined Targets"s;
    const String visualizers_dir = "Visualizers"s;
    const String all_build_name = "ALL_BUILD"s;
    const String build_dependencies_name = "BUILD_DEPENDENCIES"s;

    VSGenerator();

    void generate(const sw::SwBuild &b) override;
    //void createSolutions(Build &b) override;
    //void initSolutions(Build &b) override;
};

struct NinjaGenerator : Generator
{
    void generate(const sw::SwBuild &) override;
};

struct MakeGenerator : Generator
{
    void generate(const sw::SwBuild &b) override;
};

struct ShellGenerator : Generator
{
    bool batch = false;

    void generate(const sw::SwBuild &b) override;
};

struct CompilationDatabaseGenerator : Generator
{
    void generate(const sw::SwBuild &b) override;
};

struct SwExecutionPlan : Generator
{
    void generate(const sw::SwBuild &b) override;
};

String toString(GeneratorType Type);
String toPathString(GeneratorType Type);
GeneratorType fromString(const String &ss);
