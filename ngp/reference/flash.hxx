// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
// MAME 0.289's ngp.cpp, lines 335-544: ngp_state::flash_w, the cartridge's flash commands, as they are.
// test_flash.cpp includes it after its ngp_state, which keeps the names it uses (m_flash_chip, F_READ...), to
// check the runtime's board (ngp/machine.h) against it.

template <int Which> void ngp_state::flash_w(offs_t offset, uint8_t data)
{
	if (!m_flash_chip[Which].present)
		return;

	switch (m_flash_chip[Which].state)
	{
	case F_READ:
		if (offset == 0x5555 && data == 0xaa)
			m_flash_chip[Which].state = F_PROG1;
		m_flash_chip[Which].command[0] = 0;
		break;
	case F_PROG1:
		if (offset == 0x2aaa && data == 0x55)
			m_flash_chip[Which].state = F_PROG2;
		else
			m_flash_chip[Which].state = F_READ;
		break;
	case F_PROG2:
		if (data == 0x30)
		{
			if (m_flash_chip[Which].command[0] == 0x80)
			{
				int size = 0x10000;
				uint8_t *block = m_flash_chip[Which].data;

				m_flash_chip[Which].state = F_AUTO_BLOCK_ERASE;
				switch (m_flash_chip[Which].device_id)
				{
				case 0xab:
					if (offset < 0x70000)
						block = m_flash_chip[Which].data + (offset & 0x70000);
					else
					{
						if (offset & 0x8000)
						{
							if (offset & 0x4000)
							{
								block = m_flash_chip[Which].data + (offset & 0x7c000);
								size = 0x4000;
							}
							else
							{
								block = m_flash_chip[Which].data + (offset & 0x7e000);
								size = 0x2000;
							}
						}
						else
						{
							block = m_flash_chip[Which].data + (offset & 0x78000);
							size = 0x8000;
						}
					}
					break;
				case 0x2c:
					if (offset < 0xf0000)
						block = m_flash_chip[Which].data + (offset & 0xf0000);
					else
					{
						if (offset & 0x8000)
						{
							if (offset & 0x4000)
							{
								block = m_flash_chip[Which].data + (offset & 0xfc000);
								size = 0x4000;
							}
							else
							{
								block = m_flash_chip[Which].data + (offset & 0xfe000);
								size = 0x2000;
							}
						}
						else
						{
							block = m_flash_chip[Which].data + (offset & 0xf8000);
							size = 0x8000;
						}
					}
					break;
				case 0x2f:
					if (offset < 0x1f0000)
						block = m_flash_chip[Which].data + (offset & 0x1f0000);
					else
					{
						if (offset & 0x8000)
						{
							if (offset & 0x4000)
							{
								block = m_flash_chip[Which].data + (offset & 0x1fc000);
								size = 0x4000;
							}
							else
							{
								block = m_flash_chip[Which].data + (offset & 0x1fe000);
								size = 0x2000;
							}
						}
						else
						{
							block = m_flash_chip[Which].data + (offset & 0x1f8000);
							size = 0x8000;
						}
					}
					break;
				}
				memset(block, 0xFF, size);
			}
			else
				m_flash_chip[Which].state = F_READ;
		}
		else if (offset == 0x5555)
		{
			switch (data)
			{
			case 0x80:
				m_flash_chip[Which].command[0] = 0x80;
				m_flash_chip[Which].state = F_COMMAND;
				break;
			case 0x90:
				m_flash_chip[Which].data[0x1fc000] = m_flash_chip[Which].manufacturer_id;
				m_flash_chip[Which].data[0xfc000] = m_flash_chip[Which].manufacturer_id;
				m_flash_chip[Which].data[0x7c000] = m_flash_chip[Which].manufacturer_id;
				m_flash_chip[Which].data[0] = m_flash_chip[Which].manufacturer_id;
				m_flash_chip[Which].data[0x1fc001] = m_flash_chip[Which].device_id;
				m_flash_chip[Which].data[0xfc001] = m_flash_chip[Which].device_id;
				m_flash_chip[Which].data[0x7c001] = m_flash_chip[Which].device_id;
				m_flash_chip[Which].data[1] = m_flash_chip[Which].device_id;
				m_flash_chip[Which].data[0x1fc002] = 0x02;
				m_flash_chip[Which].data[0xfc002] = 0x02;
				m_flash_chip[Which].data[0x7c002] = 0x02;
				m_flash_chip[Which].data[2] = 0x02;
				m_flash_chip[Which].data[0x1fc003] = 0x80;
				m_flash_chip[Which].data[0xfc003] = 0x80;
				m_flash_chip[Which].data[0x7c003] = 0x80;
				m_flash_chip[Which].data[3] = 0x80;
				m_flash_chip[Which].state = F_ID_READ;
				break;
			case 0x9a:
				if (m_flash_chip[Which].command[0] == 0x9a)
					m_flash_chip[Which].state = F_BLOCK_PROTECT;
				else
				{
					m_flash_chip[Which].command[0] = 0x9a;
					m_flash_chip[Which].state = F_COMMAND;
				}
				break;
			case 0xa0:
				m_flash_chip[Which].state = F_AUTO_PROGRAM;
				break;
			case 0xf0:
			default:
				m_flash_chip[Which].state = F_READ;
				break;
			}
		}
		else
			m_flash_chip[Which].state = F_READ;
		break;
	case F_COMMAND:
		if (offset == 0x5555 && data == 0xaa)
			m_flash_chip[Which].state = F_PROG1;
		else
			m_flash_chip[Which].state = F_READ;
		break;
	case F_ID_READ:
		if (offset == 0x5555 && data == 0xaa)
			m_flash_chip[Which].state = F_PROG1;
		else
			m_flash_chip[Which].state = F_READ;
		m_flash_chip[Which].command[0] = 0;
		break;
	case F_AUTO_PROGRAM:
		/* Only 1 -> 0 changes can be programmed */
		m_flash_chip[Which].data[offset] = m_flash_chip[Which].data[offset] & data;
		m_flash_chip[Which].state = F_READ;
		break;
	case F_AUTO_CHIP_ERASE:
		m_flash_chip[Which].state = F_READ;
		break;
	case F_AUTO_BLOCK_ERASE:
		m_flash_chip[Which].state = F_READ;
		break;
	case F_BLOCK_PROTECT:
		m_flash_chip[Which].state = F_READ;
		break;
	}

	if (m_flash_chip[Which].state == F_READ)
	{
		/* Exit command/back to normal operation*/
		m_flash_chip[Which].data[0] = m_flash_chip[Which].org_data[0];
		m_flash_chip[Which].data[1] = m_flash_chip[Which].org_data[1];
		m_flash_chip[Which].data[2] = m_flash_chip[Which].org_data[2];
		m_flash_chip[Which].data[3] = m_flash_chip[Which].org_data[3];
		m_flash_chip[Which].data[0x7c000] = m_flash_chip[Which].org_data[4];
		m_flash_chip[Which].data[0x7c001] = m_flash_chip[Which].org_data[5];
		m_flash_chip[Which].data[0x7c002] = m_flash_chip[Which].org_data[6];
		m_flash_chip[Which].data[0x7c003] = m_flash_chip[Which].org_data[7];
		m_flash_chip[Which].data[0xfc000] = m_flash_chip[Which].org_data[8];
		m_flash_chip[Which].data[0xfc001] = m_flash_chip[Which].org_data[9];
		m_flash_chip[Which].data[0xfc002] = m_flash_chip[Which].org_data[10];
		m_flash_chip[Which].data[0xfc003] = m_flash_chip[Which].org_data[11];
		m_flash_chip[Which].data[0x1fc000] = m_flash_chip[Which].org_data[12];
		m_flash_chip[Which].data[0x1fc001] = m_flash_chip[Which].org_data[13];
		m_flash_chip[Which].data[0x1fc002] = m_flash_chip[Which].org_data[14];
		m_flash_chip[Which].data[0x1fc003] = m_flash_chip[Which].org_data[15];
		m_flash_chip[Which].command[0] = 0;
	}
}

