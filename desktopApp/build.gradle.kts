import org.jetbrains.compose.desktop.application.dsl.TargetFormat

plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.compose.multiplatform)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.shadow)
}

kotlin {
    jvmToolchain(17)
}

tasks.withType<com.github.jengelman.gradle.plugins.shadow.tasks.ShadowJar> {
    manifest {
        attributes["Main-Class"] = "com.loungecat.irc.MainKt"
    }
    mergeServiceFiles()
    append("META-INF/org/languagetool/language-module.properties")
}

dependencies {
    implementation(project(":shared"))
    implementation(compose.desktop.currentOs)
    implementation(compose.material3)
    implementation(compose.materialIconsExtended)
    implementation(libs.kotlinx.coroutines.core)
    implementation(libs.kotlinx.coroutines.swing)
    implementation(libs.slf4j.api)
    implementation(libs.logback.classic)
    implementation(compose.components.resources)
    implementation(libs.dorkbox.systemtray)
}

compose.desktop {
    application {
        mainClass = "com.loungecat.irc.MainKt"
        
        jvmArgs += listOf("-Xmx512m")
        
        nativeDistributions {
            targetFormats(TargetFormat.Deb, TargetFormat.AppImage, TargetFormat.Msi, TargetFormat.Rpm)
            packageName = "LoungeCat"
            packageVersion = project.version.toString()
            description = "Loungecat"
            vendor = "LoungeCat"
            
            modules("java.sql", "java.naming", "jdk.unsupported", "java.net.http")
            
            linux {
                iconFile.set(project.file("../shared/src/commonMain/composeResources/drawable/icon.png"))
                debMaintainer = "loungecat@example.com"
                menuGroup = "Network"
                appCategory = "Network"
            }
            
            windows {
                iconFile.set(project.file("icon.ico"))
                menuGroup = "Network"
                perUserInstall = true
                upgradeUuid = "68c2d829-5777-47b7-bd7b-72289c894901" // Changed to avoid overwriting old install
            }
        }
        
        buildTypes.release {
            proguard {
                configurationFiles.from(project.file("proguard-rules.pro"))
                isEnabled = false
            }
        }
    }
}
