const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Provide simdb as a C++ inclusion dependency for package manager
    const simdb_mod = b.addModule("simdb", .{
        .target = target,
        .optimize = optimize,
    });
    simdb_mod.addIncludePath(b.path("."));

    const lib_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    lib_mod.addIncludePath(b.path("."));
    lib_mod.link_libc = true;
    lib_mod.link_libcpp = true;

    const simdb_static = b.addLibrary(.{
        .linkage = .static,
        .name = "simdb",
        .root_module = lib_mod,
    });
    simdb_static.addCSourceFile(.{ .file = b.path("src/simdb.cpp"), .flags = &[_][]const u8{"-std=c++14", "-Wno-deprecated-declarations"} });
    simdb_static.installHeader(b.path("simdb.hpp"), "simdb.hpp");
    b.installArtifact(simdb_static);

    const simdb_shared = b.addLibrary(.{
        .linkage = .dynamic,
        .name = "simdb",
        .root_module = lib_mod,
    });
    simdb_shared.addCSourceFile(.{ .file = b.path("src/simdb.cpp"), .flags = &[_][]const u8{"-std=c++14", "-Wno-deprecated-declarations"} });
    b.installArtifact(simdb_shared);
}
