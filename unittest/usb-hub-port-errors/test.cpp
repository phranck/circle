// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test.cpp
 * @brief Checks that a failed request in a hub's port status change handling is logged and
 *	  survived, instead of halting the system.
 */
#include <circle/usb/usbstandardhub.h>
#include <circle/usb/xhciusbdevice.h>
#include <circle/logger.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

/// Ports of the modeled hub.
static const unsigned HubPorts = 2;

/// Port with a device connected before the hub is configured.
static const unsigned DevicePort = 2;

/// Port without a device until a test connects one.
static const unsigned FreePort = 1;

/// Name the hub device logs under, the device on root port 1.
static const char *const HubDeviceName = "usbdev1";

/**
 * @brief Returns the status change bitmap bit of a port.
 * @param nPort The 1-based port number.
 * @return Bit `nPort` set, as the hub reports a change on that port.
 */
static u16 PortBit (unsigned nPort)
{
	return static_cast<u16> (1U << nPort);
}

/**
 * @brief Fails the running test case when a condition does not hold.
 * @param bValue The condition.
 * @param pWhat What the condition states, the message of the failure.
 */
static void Check (bool bValue, const char *pWhat)
{
	if (!bValue)
	{
		throw std::runtime_error (pWhat);
	}
}

/**
 * @brief Returns whether the hub device logged an error with the given text.
 * @param pMessage The expected text.
 * @return `true` if an entry with source `HubDeviceName`, severity `LogError` and this text
 *	   was written.
 */
static bool HubLoggedError (const char *pMessage)
{
	for (const TLogEntry &Entry : CLogger::Get ()->GetEntries ())
	{
		if (   Entry.Source == HubDeviceName
		    && Entry.Severity == LogError
		    && Entry.Message == pMessage)
		{
			return true;
		}
	}

	return false;
}

/**
 * @brief A configured two-port hub with a device on `DevicePort`, waiting for status changes.
 *
 * Port enumeration does not clear the change bits it causes, so `DevicePort` starts with
 * `C_PORT_CONNECTION` and `C_PORT_RESET` set, as a real hub reports them at its first status
 * change.
 */
struct CHubFixture
{
	/// Host controller with the hub model.
	CUSBHostController Host {HubPorts};
	/// The hub's default control endpoint.
	CUSBEndpoint Endpoint0 {};
	/// The hub device.
	CUSBDevice HubDevice {HubDeviceName};
	/// The hub interface the device factory hands to the driver.
	CUSBFunction Interface {&HubDevice, &Host, &Endpoint0};
	/// The driver under test.
	CUSBStandardHub Hub {&Interface};

	/**
	 * @brief Connects the device, configures the hub and forgets the log of doing so.
	 */
	CHubFixture (void)
	{
		Host.Connect (DevicePort);
		Check (Hub.Initialize (), "hub initializes");
		Check (Hub.Configure (), "hub configures");
		Check (CXHCIUSBDevice::GetCount () == 1, "enumeration creates the device");
		CLogger::Get ()->Clear ();
	}

	/**
	 * @brief Lets the hub report a status change and handle it.
	 * @param usBitmap Bit 0 for the hub, bit n for port n.
	 */
	void Report (u16 usBitmap)
	{
		Host.ReportStatusChange (usBitmap);
		Host.UpdatePlugAndPlay ();
	}
};

/**
 * @brief Runs every test case and prints PASS or FAIL for each.
 * @return 0 when every case passed, 1 otherwise.
 */
int main (void)
{
	unsigned nPassed = 0;
	unsigned nFailed = 0;

	auto Run = [&nPassed, &nFailed] (const char *pName, const std::function<void (void)> &Test)
	{
		try
		{
			Test ();
			Check (CXHCIUSBDevice::GetCount () == 0, "fixture leaves no device behind");
			++nPassed;
			std::cout << "PASS " << pName << '\n';
		}
		catch (const std::exception &Error)
		{
			++nFailed;
			std::cout << "FAIL " << pName << ": " << Error.what () << '\n';
		}
	};

	Run ("a reported change is cleared without a log entry",
	     []
	     {
		     CHubFixture Fixture;
		     Fixture.Report (PortBit (DevicePort));
		     Check (Fixture.Host.GetChange (DevicePort) == 0, "change bits are cleared");
		     Check (CLogger::Get ()->GetEntries ().empty (), "nothing is logged");
		     Check (Fixture.Host.HasPendingRequest (), "hub waits for the next change");
	     });

	Run ("a failed clear of C_PORT_CONNECTION is logged and the hub keeps running",
	     []
	     {
		     CHubFixture Fixture;
		     Fixture.Host.FailOnce (CLEAR_FEATURE, C_PORT_CONNECTION, DevicePort);
		     Fixture.Report (PortBit (DevicePort));
		     Check (HubLoggedError ("Cannot clear C_PORT_CONNECTION (port 2)"),
			    "hub logs port and request");
		     Check (CXHCIUSBDevice::GetCount () == 1, "device stays");
		     Check (Fixture.Host.HasPendingRequest (), "hub waits for the next change");
	     });

	Run ("a change left set by a failed clear is handled when the hub reports it again",
	     []
	     {
		     CHubFixture Fixture;
		     Fixture.Host.FailOnce (CLEAR_FEATURE, C_PORT_CONNECTION, DevicePort);
		     Fixture.Report (PortBit (DevicePort));
		     Check (Fixture.Host.GetChange (DevicePort) == C_PORT_CONNECTION__MASK,
			    "connection change stays set");
		     Fixture.Report (PortBit (DevicePort));
		     Check (Fixture.Host.GetChange (DevicePort) == 0, "change bits are cleared");
		     Check (CXHCIUSBDevice::GetCount () == 1, "device stays");
	     });

	Run ("failed clears of C_PORT_ENABLE and C_PORT_RESET are logged and survived",
	     []
	     {
		     CHubFixture Fixture;
		     Fixture.Host.SetChange (DevicePort, C_PORT_ENABLE__MASK | C_PORT_RESET__MASK);
		     Fixture.Host.FailOnce (CLEAR_FEATURE, C_PORT_ENABLE, DevicePort);
		     Fixture.Host.FailOnce (CLEAR_FEATURE, C_PORT_RESET, DevicePort);
		     Fixture.Report (PortBit (DevicePort));
		     Check (HubLoggedError ("Cannot clear C_PORT_ENABLE (port 2)"),
			    "hub logs the enable change");
		     Check (HubLoggedError ("Cannot clear C_PORT_RESET (port 2)"),
			    "hub logs the reset change");
		     Check (Fixture.Host.HasPendingRequest (), "hub waits for the next change");
	     });

	Run ("a failed port status read skips that port and serves the others",
	     []
	     {
		     CHubFixture Fixture;
		     Fixture.Host.Connect (FreePort);
		     Fixture.Host.FailOnce (GET_STATUS, 0, FreePort);
		     Fixture.Report (static_cast<u16> (PortBit (FreePort) | PortBit (DevicePort)));
		     Check (HubLoggedError ("Cannot get port status (port 1)"),
			    "hub logs port and request");
		     Check (Fixture.Host.GetChange (FreePort) == C_PORT_CONNECTION__MASK,
			    "unread port keeps its change");
		     Check (Fixture.Host.GetChange (DevicePort) == 0, "other port is served");
		     Check (CXHCIUSBDevice::GetCount () == 1, "no device on the unread port yet");
		     Fixture.Report (PortBit (FreePort));
		     Check (CXHCIUSBDevice::GetCount () == 2, "device found when reported again");
	     });

	Run ("a disconnect is handled although clearing its change fails",
	     []
	     {
		     CHubFixture Fixture;
		     Fixture.Host.Disconnect (DevicePort);
		     Fixture.Host.FailOnce (CLEAR_FEATURE, C_PORT_CONNECTION, DevicePort);
		     Fixture.Report (PortBit (DevicePort));
		     Check (HubLoggedError ("Cannot clear C_PORT_CONNECTION (port 2)"),
			    "hub logs port and request");
		     Check (CXHCIUSBDevice::GetCount () == 0, "device is removed");
		     Check (Fixture.Host.HasPendingRequest (), "hub waits for the next change");
	     });

	std::cout << nPassed << " passed, " << nFailed << " failed\n";

	return nFailed != 0 ? 1 : 0;
}
