plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.compose.multiplatform)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.ksp)
    alias(libs.plugins.room)
}

kotlin {
    jvm("desktop")
    
    sourceSets {
        val commonMain by getting {
            dependencies {
                implementation(compose.runtime)
                implementation(compose.foundation)
                implementation(compose.material3)
                implementation(compose.materialIconsExtended)
                implementation(compose.components.resources)
                
                implementation(libs.kotlinx.coroutines.core)
                implementation(libs.kotlinx.serialization.json)
                implementation(libs.kitteh.irc)
                implementation(libs.jsoup)
                implementation(libs.coil.compose)
                implementation(libs.coil.network.okhttp)
                implementation(libs.datastore.preferences.core)
                
                implementation(libs.room.runtime)
                implementation(libs.sqlite.bundled)
            }
        }
        
        val desktopMain by getting {
            dependencies {
                implementation(compose.desktop.currentOs)
                implementation(libs.kotlinx.coroutines.swing)
                implementation(libs.slf4j.api)
                implementation(libs.logback.classic)
                implementation(libs.sqlite.jdbc)
                implementation(libs.languagetool.core)
                implementation(libs.languagetool.en)
                implementation(libs.languagetool.de)
                implementation(libs.languagetool.nl)
                implementation(libs.languagetool.pl)
            }
        }

        val desktopTest by getting {
            dependencies {
                implementation(kotlin("test"))
                implementation(kotlin("test-junit"))
                implementation(libs.junit)
            }
        }
    }
}

room {
    schemaDirectory("$projectDir/schemas")
}

dependencies {
    add("kspDesktop", libs.room.compiler)
}

compose.resources {
    publicResClass = true
    packageOfResClass = "com.loungecat.irc.shared.generated.resources"
}
