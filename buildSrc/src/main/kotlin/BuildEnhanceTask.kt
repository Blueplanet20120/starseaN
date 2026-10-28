// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

import org.gradle.api.DefaultTask
import org.gradle.api.GradleException
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.LocalState
import org.gradle.api.tasks.Optional
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.process.ExecOperations
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.Properties
import javax.inject.Inject

abstract class BuildEnhanceTask : DefaultTask() {
    @get:Inject
    abstract val execOperations: ExecOperations

    @get:Internal
    abstract val sourceDirectory: DirectoryProperty

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sourceFiles: ConfigurableFileCollection

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @get:LocalState
    abstract val workDirectory: DirectoryProperty

    @get:Optional
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val localPropertiesFile: RegularFileProperty

    @get:Input
    abstract val minSdk: Property<Int>

    @get:Input
    abstract val targetAbis: ListProperty<String>

    init {
        group = "resources"
        description = "Build the application-list enhancement module for every app ABI."
    }

    @TaskAction
    fun build() {
        val properties = Properties()
        localPropertiesFile.orNull?.asFile?.takeIf(File::exists)?.inputStream()?.use(properties::load)
        val sdkDir = properties.getProperty("sdk.dir")?.takeIf(String::isNotBlank)?.let(::File)
            ?: listOf("ANDROID_HOME", "ANDROID_SDK_ROOT").firstNotNullOfOrNull { name ->
                System.getenv(name)?.takeIf(String::isNotBlank)?.let(::File)
            }
            ?: throw GradleException("Android SDK location is missing.")
        val ndkDir = properties.getProperty("ndk.dir")?.takeIf(String::isNotBlank)?.let(::File)
            ?: listOf("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT").firstNotNullOfOrNull { name ->
                System.getenv(name)?.takeIf(String::isNotBlank)?.let(::File)
            }
            ?: sdkDir.resolve("ndk").listFiles()?.filter(File::isDirectory)?.maxByOrNull(File::getName)
            ?: throw GradleException("Android NDK location is missing.")
        val sourceDir = sourceDirectory.get().asFile
        val script = sourceDir.resolve("tools/build.py")
        if (!script.isFile) throw GradleException("AsteriskEnhance source is missing: $sourceDir")
        val python = resolvePython(properties)
        logger.lifecycle("enhance python: $python")
        execOperations.exec {
            workingDir(sourceDir)
            commandLine(
                python, script.absolutePath,
                "--ndk", ndkDir.absolutePath,
                "--sdk", sdkDir.absolutePath,
                "--min-sdk", minSdk.get().toString(),
                "--work-directory", workDirectory.get().asFile.absolutePath,
                "--output-directory", outputDirectory.get().asFile.absolutePath,
                "--skip-package",
                "--abis", *targetAbis.get().toTypedArray(),
            )
        }.assertNormalExitValue()
    }

    private fun resolvePython(properties: Properties): String {
        val candidates = linkedSetOf<String>()
        properties.getProperty("python.dir")?.trim()?.takeIf(String::isNotEmpty)?.let { configured ->
            val file = File(configured)
            candidates += if (file.isDirectory) File(file, pythonExecutableName()).absolutePath else file.absolutePath
        }
        System.getenv("PYTHON")?.trim()?.takeIf(String::isNotEmpty)?.let(candidates::add)
        if (isWindows()) {
            windowsPythonLaunchers().forEach(candidates::add)
            uvPythonInstalls().forEach(candidates::add)
        }
        candidates += "python"
        val usable = candidates.mapNotNull(::pythonIdentity)
        val cpython = usable.filter { identity -> identity.cpython && identity.atLeast310 }
        if (cpython.isNotEmpty()) {
            return cpython.minWith(compareBy<PythonIdentity> { it.major }.thenBy { it.minor }).path
        }
        val newer = usable.filter(PythonIdentity::atLeast310)
        if (newer.isNotEmpty()) {
            return newer.minWith(compareBy<PythonIdentity> { it.major }.thenBy { it.minor }).path
        }
        return "python"
    }

    private fun pythonIdentity(path: String): PythonIdentity? {
        val output = ByteArrayOutputStream()
        val result = execOperations.exec {
            commandLine(
                path, "-c",
                "import sys; print(sys.version_info[0], sys.version_info[1], sys.implementation.name)",
            )
            standardOutput = output
            errorOutput = ByteArrayOutputStream()
            isIgnoreExitValue = true
        }
        if (result.exitValue != 0) return null
        val parts = output.toString(Charsets.UTF_8).trim().split(Regex("\\s+"))
        if (parts.size < 3) return null
        val major = parts[0].toIntOrNull() ?: return null
        val minor = parts[1].toIntOrNull() ?: return null
        return PythonIdentity(path, major, minor, parts[2] == "cpython")
    }

    private fun windowsPythonLaunchers(): List<String> {
        val output = ByteArrayOutputStream()
        val result = execOperations.exec {
            commandLine("py", "-0p")
            standardOutput = output
            errorOutput = ByteArrayOutputStream()
            isIgnoreExitValue = true
        }
        if (result.exitValue != 0) return emptyList()
        return output.toString(Charsets.UTF_8).lineSequence().mapNotNull { line ->
            val path = line.trim().substringAfterLast("  ", "").trim()
            path.takeIf { it.endsWith("python.exe", ignoreCase = true) && File(it).isFile }
        }.toList()
    }

    private fun uvPythonInstalls(): List<String> {
        val appData = System.getenv("APPDATA")?.takeIf(String::isNotBlank) ?: return emptyList()
        val root = File(appData, "uv/python")
        if (!root.isDirectory) return emptyList()
        return root.listFiles()?.mapNotNull { install ->
            install.resolve("python.exe").takeIf(File::isFile)?.absolutePath
        }.orEmpty()
    }

    private fun isWindows(): Boolean {
        return System.getProperty("os.name").startsWith("Windows", ignoreCase = true)
    }

    private fun pythonExecutableName(): String {
        return if (isWindows()) "python.exe" else "python"
    }
}

private data class PythonIdentity(
    val path: String,
    val major: Int,
    val minor: Int,
    val cpython: Boolean,
) {
    val atLeast310: Boolean
        get() = major > 3 || (major == 3 && minor >= 10)
}

