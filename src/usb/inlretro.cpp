#include "inlretro.h"

enum INLRequest {
	requestIO         = 0x02,
	requestSNES       = 0x04,
	requestBuffer     = 0x05,
	requestOperation  = 0x07,
	requestBootloader = 0x0a,
};

// IO opcodes
#define IO_RESET  0x0000
#define SNES_INIT 0x0002

// SNES cart opcodes
#define SNES_SET_BANK   0x0000
#define SNES_ROM_RD     0x0001
#define SNES_ROM_WR(b) ((b<<8)|0x0002)
#define SNES_SYS_RD     0x0005
#define SNES_SYS_WR(b) ((b<<8)|0x0006)

// buffer opcodes
#define RAW_BUFFER_RESET        0x0000
#define SET_MEM_N_PART(b)       ((b<<8)|0x0030)
	#define SNESROM_PAGE 0x24
	#define MASKROM 0xdd
#define SET_MAP_N_MAPVAR(b)     ((b<<8)|0x0032)
#define GET_CUR_BUFF_STATUS     0x0061
	#define STATUS_DUMPED 0xd8
#define BUFF_PAYLOAD            0x0070
#define ALLOCATE_BUFFER(n,b)    ((b<<8)|0x0080|n)
#define SET_RELOAD_PAGENUM(n,b) ((b<<8)|0x0090|n)

// operations
#define SET_OPERATION 0x0000
	#define OPERATION_RESET     0x0001
	#define OPERATION_STARTDUMP 0x00d2

// bootloader opcodes
#define GET_APP_VER 0x000c // len = 3

// ----------------------------------------------------------------------------
INLRetroDevice::INLRetroDevice(QObject *parent)
	: USBDevice(0x16c0, 0x05dc, parent)
{
	currentBank = 0;
}

// ----------------------------------------------------------------------------
bool INLRetroDevice::open()
{
	if (USBDevice::open())
	{
		try
		{
			// TODO: validate vendor & product names

			// TODO : check return values of these
			writeControlPacket(requestBootloader, GET_APP_VER, 0, 3);
			// TODO: check returned data

			writeControlPacket(requestIO, IO_RESET, 0);
			writeControlPacket(requestIO, SNES_INIT, 0);

			currentBank = 0;

			return true;
		}
		catch (TimeoutException&) {}
	}

	close();
	return false;
}

// ----------------------------------------------------------------------------
quint8 INLRetroDevice::readByte(quint8 bank, quint16 addr, bool *ok)
{
	bool bOk = false;
	quint8 val = 0;

	quint16 value = SNES_SYS_RD;
	if ((bank & 0x40) || (addr & 0x8000))
	{
		value = SNES_ROM_RD; // assert ROMSEL
	}

	try
	{
		setBank(bank); 
		writeControlPacket(requestSNES, value, addr, 3);

		if (inData[0] == '\x00' && inData[1] == '\x01')
		{
			val = inData[2];
			bOk = true;
		}
	}
	catch (TimeoutException&) {}

	if (ok) *ok = bOk;
	return val;
}

// ----------------------------------------------------------------------------
QByteArray INLRetroDevice::readBytes(quint8 bank, quint16 addr, unsigned size, bool *ok)
{
	bool bOk = false;
	QByteArray readData;

	try
	{
		setBank(bank);

		// reset buffers
		writeControlPacket(requestOperation, SET_OPERATION, OPERATION_RESET);
		writeControlPacket(requestBuffer,    RAW_BUFFER_RESET, 0);

		// initialize buffers for 128 byte reads
		// buffer 0: id 0x00, bank offset 0x00, reload 1
		// buffer 1: id 0x80, bank offset 0x04, reload 1
		writeControlPacket(requestBuffer, ALLOCATE_BUFFER(0, 4), 0x0000);
		writeControlPacket(requestBuffer, ALLOCATE_BUFFER(1, 4), 0x8004);
		writeControlPacket(requestBuffer, SET_RELOAD_PAGENUM(0, 1), 0x0000);
		writeControlPacket(requestBuffer, SET_RELOAD_PAGENUM(1, 1), 0x0000);

		// set up buffer read pointers
		// mapper number = CPU addr page number, mapper variation = always 0
		// TODO: handle start addresses that aren't page-aligned
		writeControlPacket(requestBuffer, SET_MEM_N_PART(0), (SNESROM_PAGE << 8) | MASKROM);
		writeControlPacket(requestBuffer, SET_MEM_N_PART(1), (SNESROM_PAGE << 8) | MASKROM);
		writeControlPacket(requestBuffer, SET_MAP_N_MAPVAR(0), addr & 0xff00);
		writeControlPacket(requestBuffer, SET_MAP_N_MAPVAR(1), addr & 0xff00);

		// start dump
		writeControlPacket(requestOperation, SET_OPERATION, OPERATION_STARTDUMP);

		while (readData.size() < size)
		{
			// wait for read buffer
			do
			{
				writeControlPacket(requestBuffer, GET_CUR_BUFF_STATUS, 0, 3);
			} while (inData.size() < 3 && inData[2] != (char)STATUS_DUMPED);

			// get data
			writeControlPacket(requestBuffer, BUFF_PAYLOAD, 0, 128);
			readData += inData;
		}

		// we're finished; get out of dump mode again
		writeControlPacket(requestOperation, SET_OPERATION, OPERATION_RESET);
		writeControlPacket(requestBuffer, RAW_BUFFER_RESET, 0);

		bOk = true;
	}
	catch (TimeoutException&) {}

	if (ok) *ok = bOk;
	return readData;
}

// ----------------------------------------------------------------------------
bool INLRetroDevice::writeByte(quint8 bank, quint16 addr, quint8 data)
{
	quint16 value = SNES_SYS_WR(data);
	if ((bank & 0x40) || (addr & 0x8000))
	{
		value = SNES_ROM_WR(data); // assert ROMSEL
	}

	try
	{
		setBank(bank);
		writeControlPacket(requestSNES, value, addr);
		return true;
	}
	catch (TimeoutException&) {}

	return false;
}

// ----------------------------------------------------------------------------
void INLRetroDevice::setBank(quint8 bank)
{
	if (bank != currentBank)
	{
		writeControlPacket(requestSNES, SNES_SET_BANK, bank);
		currentBank = bank;
	}
}
