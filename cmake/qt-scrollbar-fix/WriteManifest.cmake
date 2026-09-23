file(SHA256 "${RUNTIME_FILE}" runtime_sha256)
file(WRITE "${RUNTIME_FILE}.json"
    "{\n  \"qtVersion\": \"${QT_VERSION}\",\n  \"patch\": \"APP-001-scrollbar-unique-connection-v1\",\n  \"configuration\": \"${RUNTIME_CONFIG}\",\n  \"sourceSha256\": \"${SOURCE_SHA256}\",\n  \"runtimeSha256\": \"${runtime_sha256}\"\n}\n")
