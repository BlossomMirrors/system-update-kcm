#pragma once

/*  Session bus service that owns OS upgrade transactions so they, and their
 *  Plasma job notification, outlive the System Settings window
*/
static constexpr auto HELPER_SERVICE = "org.blossomos.SoftwareUpdateHelper";
static constexpr auto HELPER_PATH    = "/org/blossomos/SoftwareUpdateHelper";
static constexpr auto HELPER_IFACE   = "org.blossomos.SoftwareUpdateHelper";
