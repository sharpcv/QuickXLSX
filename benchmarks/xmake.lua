target("benchmark")
    set_kind("binary")
    set_languages("c++20")
    add_files("bench_stream.cpp")
    add_deps("quickxlsx")
target_end()

if has_config("quickxlsx_xlsx") then
    target("benchmark_quickxlsx")
        set_kind("binary")
        set_languages("c++20")
        add_files("bench_quickxlsx.cpp")
        add_deps("quickxlsx")
    target_end()

    -- openxlsx/xlnt are only declared as requirements when quickxlsx_benchmarks is on
    if has_config("quickxlsx_benchmarks") then
        target("benchmark_openxlsx")
            set_kind("binary")
            set_languages("c++20")
            add_files("bench_openxlsx.cpp")
            add_packages("openxlsx")
        target_end()

        target("benchmark_xlnt")
            set_kind("binary")
            set_languages("c++20")
            add_files("bench_xlnt.cpp")
            add_packages("xlnt")
        target_end()

        target("benchmark_compare")
            set_kind("binary")
            set_languages("c++20")
            add_files("bench_compare.cpp")
            add_deps("benchmark_quickxlsx", "benchmark_openxlsx", "benchmark_xlnt")
        target_end()
    end
end
