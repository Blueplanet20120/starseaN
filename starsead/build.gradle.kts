// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

plugins {
    alias(libs.plugins.android.library)
}

val generatedJniLibsDir: Provider<Directory> = layout.buildDirectory.dir("generated/jniLibs")
val starseadSubmoduleDir = layout.projectDirectory.dir("src/main/native")

android {
    namespace = "starsead"
    compileSdk = ProjectConfig.TARGET_SDK

    defaultConfig {
        minSdk = ProjectConfig.MIN_SDK
        ndk {
            abiFilters += nativeAndroidAbis()
        }
    }

    androidComponents {
        onVariants { variant ->
            variant.sources.jniLibs?.addStaticSourceDirectory("build/generated/jniLibs")
        }
    }

    lint {
        disable += "ChromeOsAbiSupport"
    }
}

val syncStarseadVersion = tasks.register<SyncGitSubmoduleVersionTask>("syncStarseadVersion") {
    submoduleVersion.set(ProjectConfig.STARSEAD_VERSION)
    repositoryRootDirectory.set(rootProject.layout.projectDirectory)
    submoduleDirectory.set(starseadSubmoduleDir)
    submodulePath.set(starseadSubmoduleDir.asFile.relativeTo(rootProject.projectDir).invariantSeparatorsPath)
}

val buildStarsead = tasks.register<BuildStarseadTask>("buildStarsead") {
    dependsOn(syncStarseadVersion)
    sourceDirectory.set(starseadSubmoduleDir)
    outputDirectory.set(generatedJniLibsDir)
    rootProject.layout.projectDirectory.file("local.properties")
        .takeIf { it.asFile.exists() }
        ?.let(localPropertiesFile::set)
    minSdk.set(ProjectConfig.MIN_SDK)
    targetAbis.set(nativeAndroidAbis())
}

tasks.named("preBuild") {
    dependsOn(buildStarsead)
}
