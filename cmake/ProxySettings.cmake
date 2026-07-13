if(WIN32 AND NOT DEFINED ENV{HTTP_PROXY})
    execute_process(
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyEnable
        OUTPUT_VARIABLE REG_PROXY_ENABLE
        ERROR_QUIET
    )
    if(REG_PROXY_ENABLE MATCHES "0x1")
        execute_process(
            COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings" /v ProxyServer
            OUTPUT_VARIABLE REG_PROXY_SERVER
            ERROR_QUIET
        )
        if(REG_PROXY_SERVER)
            string(REGEX REPLACE ".*REG_SZ[ \t]+([^\r\n]+).*" "\\1" PROXY_SERVER "${REG_PROXY_SERVER}")
            string(STRIP ${PROXY_SERVER} PROXY_SERVER)

            if(PROXY_SERVER MATCHES "^http://")
                string(REPLACE "http://" "" PROXY_SERVER "${PROXY_SERVER}")
            elseif(PROXY_SERVER MATCHES "^https://")
                string(REPLACE "https://" "" PROXY_SERVER "${PROXY_SERVER}")
            endif()

            if(PROXY_SERVER MATCHES ":[0-9]+$")
                set(HTTP_PROXY "http://${PROXY_SERVER}")
                set(HTTPS_PROXY "http://${PROXY_SERVER}")
                set(ENV{HTTP_PROXY} ${HTTP_PROXY})
                set(ENV{HTTPS_PROXY} ${HTTPS_PROXY})
                set(ENV{NO_PROXY} "localhost,127.0.0.1")
                message(STATUS "Proxy detected from system settings: ${HTTP_PROXY}")
            endif()
        endif()
    endif()
endif()
