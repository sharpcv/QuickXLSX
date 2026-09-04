package("minizip-ng")
    set_homepage("https://github.com/zlib-ng/minizip-ng")
    set_license("zlib")
    set_urls("$(projectdir)/third_party/sources/minizip-ng-$(version).tar.gz")
    add_versions("4.0.10", "c362e35ee973fa7be58cc5e38a4a6c23cc8f7e652555daf4f115a9eb2d3a6be7")
    add_deps("cmake", "zlib-ng")
    add_configs("zlib", {default = true, type = "boolean"})
    add_configs("bzip2", {default = false, type = "boolean"})
    add_configs("lzma", {default = false, type = "boolean"})
    add_configs("zstd", {default = false, type = "boolean"})
    on_install(function (package)
        import("package.tools.cmake").install(package, {
            "-DMZ_ZLIB=ON", "-DMZ_BZIP2=OFF", "-DMZ_LZMA=OFF", "-DMZ_ZSTD=OFF",
            "-DMZ_OPENSSL=OFF", "-DMZ_LIBBSD=OFF", "-DMZ_BUILD_TESTS=OFF",
            "-DMZ_BUILD_UNIT_TESTS=OFF", "-DMZ_BUILD_FUZZ_TESTS=OFF",
            "-DMZ_BUILD_TOOLS=OFF", "-DBUILD_SHARED_LIBS=OFF"})
    end)
