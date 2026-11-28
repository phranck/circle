// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file koptions.h
 * @brief Host replacement for Circle's kernel options, of which none are set.
 */
#ifndef _circle_koptions_h
#define _circle_koptions_h

/**
 * @brief Kernel options that do not exist, so the driver uses its defaults.
 */
class CKernelOptions
{
public:
	/**
	 * @brief Returns no options object, as before the kernel has parsed `cmdline.txt`.
	 * @return Always null.
	 */
	static CKernelOptions *Get (void)
	{
		return nullptr;
	}

	/**
	 * @brief Returns the configured USB power delay.
	 * @return Always 0, which means none is configured.
	 */
	unsigned GetUSBPowerDelay (void) const
	{
		return 0;
	}
};

#endif
