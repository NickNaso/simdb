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

    const simdb_static = b.addLibrary(.{
        .linkage = .static,
        .name = "simdb",
        .target = target,
        .optimize = optimize,
    });
    simdb_static.root_module.addIncludePath(b.path("."));
    simdb_static.root_module.link_libc = true;
    simdb_static.root_module.link_libcpp = true;
    simdb_static.addCSourceFile(.{ .file = b.path("src/simdb.cpp"), .flags = &[_][]const u8{"-std=c++14", "-Wno-deprecated-declarations"} });
    simdb_static.installHeader(b.path("simdb.hpp"), "simdb.hpp");
    b.installArtifact(simdb_static);

    const simdb_shared = b.addLibrary(.{
        .linkage = .dynamic,
        .name = "simdb",
        .target = target,
        .optimize = optimize,
    });
    simdb_shared.root_module.addIncludePath(b.path("."));
    simdb_shared.root_module.link_libc = true;
    simdb_shared.root_module.link_libcpp = true;
    simdb_shared.addCSourceFile(.{ .file = b.path("src/simdb.cpp"), .flags = &[_][]const u8{"-std=c++14", "-Wno-deprecated-declarations"} });
    b.installArtifact(simdb_shared);
}
