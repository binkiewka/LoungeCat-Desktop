FROM eclipse-temurin:17-jdk-alpine AS build

WORKDIR /app

COPY gradle gradle
COPY gradlew .
COPY build.gradle.kts .
COPY settings.gradle.kts .
COPY shared shared
COPY desktopApp desktopApp

RUN chmod +x gradlew

RUN ./gradlew :desktopApp:shadowJar --no-daemon

FROM eclipse-temurin:17-jre

# Install necessary X11 and font libraries
RUN apt-get update && apt-get install -y \
    libxext6 \
    libxrender1 \
    libxtst6 \
    libxi6 \
    libfreetype6 \
    libgl1 \
    fontconfig \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app/desktopApp/build/libs/*-all.jar app.jar

RUN mkdir -p /data

VOLUME ["/data"]

ENV LOUNGECAT_DB_PATH=/data/loungecat.db
ENV LOUNGECAT_CONFIG=/data/preferences.json

# Default to 2x scaling if not specified (override in docker-compose for dynamic behavior)
ENV JAVA_OPTS="-Xmx1g -Xms512m -Dsun.java2d.uiScale=2"

ENTRYPOINT ["sh", "-c", "java $JAVA_OPTS -jar app.jar"]
