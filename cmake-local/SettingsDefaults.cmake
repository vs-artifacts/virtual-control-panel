# Turns source/SettingsDefaults.json (every plugin-wide setting's factory default and limits) into the C++ header
# SettingsDefaults.h; the page imports the same file. Editing the JSON reconfigures; a malformed entry stops the build.

set(SETTINGS_DEFAULTS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/source/SettingsDefaults.json")
set(SETTINGS_DEFAULTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SETTINGS_DEFAULTS_JSON}")

file(READ "${SETTINGS_DEFAULTS_JSON}" settingsJson)
string(JSON settingsCount LENGTH "${settingsJson}")
math(EXPR lastSetting "${settingsCount} - 1")

set(wholePattern "^-?[0-9]+$")
set(decimalPattern "^-?[0-9]+(\\.[0-9]+)?$")
set(SETTINGS_DEFAULTS_ENTRIES "")
set(SETTINGS_DEFAULTS_ALL "")

function(settings_defaults_fail key problem)
    message(FATAL_ERROR "source/SettingsDefaults.json, \"${key}\": ${problem}")
endfunction()

foreach(index RANGE ${lastSetting})
    string(JSON key MEMBER "${settingsJson}" ${index})

    if (NOT key MATCHES "^[a-z][A-Za-z0-9]*$")
        settings_defaults_fail("${key}" "a name must be letters and digits, starting with a lowercase letter")
    endif ()

    string(JSON type GET "${settingsJson}" "${key}" type)
    string(JSON default GET "${settingsJson}" "${key}" default)

    if (type STREQUAL "flag")
        string(JSON defaultType TYPE "${settingsJson}" "${key}" default)

        if (NOT defaultType STREQUAL "BOOLEAN")
            settings_defaults_fail("${key}" "a flag's default must be true or false")
        endif ()

        if (default)
            set(default "true")
        else ()
            set(default "false")
        endif ()

        string(APPEND SETTINGS_DEFAULTS_ENTRIES "    inline constexpr Flag ${key} { \"${key}\", ${default} };\n")
    elseif (type STREQUAL "whole" OR type STREQUAL "decimal")
        string(JSON min GET "${settingsJson}" "${key}" min)
        string(JSON max GET "${settingsJson}" "${key}" max)

        if (type STREQUAL "whole")
            set(pattern "${wholePattern}")
            set(struct "Whole")
        else ()
            set(pattern "${decimalPattern}")
            set(struct "Decimal")
        endif ()

        foreach(number IN ITEMS "${default}" "${min}" "${max}")
            if (NOT number MATCHES "${pattern}")
                settings_defaults_fail("${key}" "\"${number}\" isn't a ${type} number")
            endif ()
        endforeach()

        string(APPEND SETTINGS_DEFAULTS_ENTRIES
            "    inline constexpr ${struct} ${key} { \"${key}\", ${default}, ${min}, ${max} };\n"
            "    static_assert (${key}.min <= ${key}.max && ${key}.allows (${key}.value), \"${key}: the default must be within its limits\");\n")
    elseif (type STREQUAL "choice")
        string(JSON choiceCount LENGTH "${settingsJson}" "${key}" choices)

        if (choiceCount LESS 1)
            settings_defaults_fail("${key}" "a choice needs choices")
        endif ()

        math(EXPR lastChoice "${choiceCount} - 1")
        set(choices "")

        foreach(choiceIndex RANGE ${lastChoice})
            string(JSON choice GET "${settingsJson}" "${key}" choices ${choiceIndex})

            if (NOT choice MATCHES "^[A-Za-z0-9]+$")
                settings_defaults_fail("${key}" "a choice must be letters and digits")
            endif ()

            list(APPEND choices "\"${choice}\"")
        endforeach()

        list(JOIN choices ", " choices)
        string(APPEND SETTINGS_DEFAULTS_ENTRIES
            "    inline constexpr std::array<std::string_view, ${choiceCount}> ${key}Choices { ${choices} };\n"
            "    inline constexpr Choice ${key} { \"${key}\", \"${default}\", ${key}Choices };\n"
            "    static_assert (${key}.allows (${key}.value), \"${key}: the default must be one of its choices\");\n")
    else ()
        settings_defaults_fail("${key}" "type must be flag, whole, decimal or choice")
    endif ()

    string(APPEND SETTINGS_DEFAULTS_ALL "        Setting { ${key} },\n")
endforeach()

configure_file("${CMAKE_CURRENT_LIST_DIR}/SettingsDefaults.h.in" "${SETTINGS_DEFAULTS_DIR}/SettingsDefaults.h" @ONLY)
