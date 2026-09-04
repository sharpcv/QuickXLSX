set_project("QuickXLSX")
set_version("0.1.0")
set_xmakever("2.8.0")

add_rules("mode.debug", "mode.release")
set_languages("c++20")
add_repositories("quickxlsx third_party")

option("quickxlsx_xlsx")
    set_default(true)
    set_showmenu(true)
    set_description("Enable XLSX read/write support (ZIP, DEFLATE, and XML dependencies)")
option_end()

option("quickxlsx_simd")
    set_default(true)
    set_showmenu(true)
    set_description("Enable portable SIMD-assisted UTF-8 and DEFLATE implementations")
option_end()

option("quickxlsx_fast_float")
    set_default(true)
    set_showmenu(true)
    set_description("Use fast_float for locale-independent floating-point parsing")
option_end()

option("quickxlsx_fmt")
    set_default(true)
    set_showmenu(true)
    set_description("Use fmt for optional date/time formatting support")
option_end()

option("quickxlsx_native")
    set_default(false)
    set_showmenu(true)
    set_description("Optimize for the build host CPU; unsuitable for portable release artifacts")
option_end()

option("quickxlsx_benchmarks")
    set_default(false)
    set_showmenu(true)
    set_description("Build standalone CSV/XLSX throughput benchmarks")
option_end()
if has_config("quickxlsx_benchmarks") and has_config("quickxlsx_xlsx") then
    add_requires("openxlsx", "xlnt")
end

if has_config("quickxlsx_xlsx") then
    add_requires("zlib-ng 2.3.3", {
        configs = {zlib_compat = true}
    })
    add_requires("minizip-ng 4.0.10", {
        configs = {
            zlib = true,
            bzip2 = false,
            lzma = false,
            zstd = false
        }
    })
    add_requires("pugixml 1.15", {
        configs = {
            wchar = false,
            exceptions = true,
            headeronly = false
        }
    })
end

if has_config("quickxlsx_simd") then
    add_requires("simdutf 8.2.0")
end

if has_config("quickxlsx_fast_float") then
    add_requires("fast_float 8.2.1")
end

if has_config("quickxlsx_fmt") then
    add_requires("fmt 10.2.1")
end

target("quickxlsx")
    set_kind("static")
    set_languages("c++20")
    add_cxflags("/utf-8", {tools = {"cl"}, public = true})
    if has_config("quickxlsx_xlsx") then
        add_files("src/**.cpp")
    else
        add_files("src/**.cpp")
        remove_files("src/zip.cpp", "src/xml.cpp", "src/xlsx_reader.cpp", "src/xlsx_writer.cpp")
    end
    add_headerfiles("include/(quickxlsx/**.hpp)")
    add_includedirs("include", {public = true})

    if has_config("quickxlsx_xlsx") then
        add_packages("zlib-ng", "minizip-ng", "pugixml", {public = true})
        add_defines("QUICKXLSX_ENABLE_XLSX", {public = true})
    end

    if has_config("quickxlsx_simd") then
        add_packages("simdutf", {public = true})
        add_defines("QUICKXLSX_ENABLE_SIMD", {public = true})
    end

    if has_config("quickxlsx_fast_float") then
        add_packages("fast_float", {public = true})
        add_defines("QUICKXLSX_USE_FAST_FLOAT", {public = true})
    end

    if has_config("quickxlsx_fmt") then
        add_packages("fmt", {public = true})
        add_defines("QUICKXLSX_USE_FMT", {public = true})
    end

    if has_config("quickxlsx_native") then
        add_cxflags("-march=native", {tools = {"gcc", "clang"}})
    end

target_end()

if os.isfile("tests/xmake.lua") then
    includes("tests/xmake.lua")
end

if has_config("quickxlsx_benchmarks") or os.isfile("benchmarks/xmake.lua") then
    includes("benchmarks/xmake.lua")
end
