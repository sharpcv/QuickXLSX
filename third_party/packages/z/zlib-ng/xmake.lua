package("zlib-ng")
    set_homepage("https://github.com/zlib-ng/zlib-ng")
    set_license("zlib")
    set_urls("$(projectdir)/third_party/sources/zlib-ng-$(version).tar.gz")
    add_versions("2.3.3", "f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1")
    add_configs("zlib_compat", {default = true, type = "boolean"})
    add_deps("cmake")
    on_install(function (package)
        import("package.tools.cmake").install(package, {
            "-DZLIB_COMPAT=" .. (package:config("zlib_compat") and "ON" or "OFF"),
            "-DZLIB_ENABLE_TESTS=OFF", "-DZLIBNG_ENABLE_TESTS=OFF",
            "-DWITH_GTEST=OFF", "-DBUILD_SHARED_LIBS=OFF"})
    end)
