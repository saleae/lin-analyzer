#include "LINSimulationDataGenerator.h"
#include "LINAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

#if defined(__GNUC__)
	#include <stdlib.h>
#endif

LINSimulationDataGenerator::LINSimulationDataGenerator()
{
}

LINSimulationDataGenerator::~LINSimulationDataGenerator()
{
}

void LINSimulationDataGenerator::Initialize( U32 simulation_sample_rate, LINAnalyzerSettings* settings )
{
	mSimulationSampleRateHz = simulation_sample_rate;
	mSettings = settings;

	mSerialSimulationData.SetChannel( mSettings->mInputChannel );
	mSerialSimulationData.SetSampleRate( simulation_sample_rate );
	mSerialSimulationData.SetInitialBitState( BIT_HIGH );
}

U32 LINSimulationDataGenerator::GenerateSimulationData( U64 largest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channel )
{
	U64 adjusted_largest_sample_requested = AnalyzerHelpers::AdjustSimulationTargetSample( largest_sample_requested, sample_rate, mSimulationSampleRateHz );

	while( mSerialSimulationData.GetCurrentSampleNumber() < adjusted_largest_sample_requested )
	{
		CreateFrame();
		//CreateFrame();
		//CreateBadFrame();
	}

	*simulation_channel = &mSerialSimulationData;
	return 1;
}

void LINSimulationDataGenerator::CreateFrame()
{
	U32 samples_per_bit = mSimulationSampleRateHz / mSettings->mBitRate;
    mSerialSimulationData.Advance( samples_per_bit * Random(1,4) ); // simulate jitter
	CreateHeader();
	if ( mSettings->mLINVersion < 2 )
		mChecksum.clear(); // Version 1 starts chksum at first data byte
	CreateReponse(Random(1,8));
}

void LINSimulationDataGenerator::CreateBadFrame()
{
	U32 samples_per_bit = mSimulationSampleRateHz / mSettings->mBitRate;
	mSerialSimulationData.Advance( samples_per_bit * Random( 1, 4 ) ); // simulate jitter
	U8 identifier = CreateHeader();
	bool classic_identifier = false;
	if( identifier == 0x3C || identifier == 0x3D )
		classic_identifier == true;

	if( mSettings->mLINVersion < 2 || classic_identifier == true )
		mChecksum.clear(); // Version 1 starts chksum at first data byte.
	U32 data_count = Random( 1, 8 );

	data_count = 5;
	for( U8 i = 0; i < data_count && i < 8; i++ )
	{
		CreateSerialByte( static_cast<U8>( Random( 0, 255 ) & 0xFF ) );
	}
	CreateSerialByte( mChecksum.result() + 3 );

}

U8 LINSimulationDataGenerator::CreateHeader()
{
	CreateBreakField();
	CreateSyncField();
	mChecksum.clear(); // version 2 starts chksum at PID field.
	U8 identifier = Random( 0, 59 );

	if( Random( 1, 6 ) == 6 )
		identifier = 0x3C;
	if( Random( 1, 6 ) == 5 )
		identifier = 0x3D;

	CreateProtectedIdentifierField( identifier );

	return identifier;
}

void LINSimulationDataGenerator::CreateReponse(U8 length)
{
	for( U8 i=0; i < length && i < 8; i++ )
	{
		U8 data_byte = Random( 0, 255 ) & 0xFF;

		if( i >= 1 && Random( 1, 5 ) == 5 )
			data_byte = mChecksum.result();

		CreateSerialByte( data_byte );
	}
	CreateSerialByte(mChecksum.result());
}

void LINSimulationDataGenerator::CreateBreakField()
{
		// The break field.
	U32 samples_per_bit = (mSimulationSampleRateHz / mSettings->mBitRate) + 1; //to fix round off error
	U8 byte1 = 0x0;
	U8 byte2 = 0xE0;

	mChecksum.add( byte1 );
	SwapEnds( byte1 );

	mChecksum.add( byte2 );
	SwapEnds( byte2 );

	// inter-byte space.....
	mSerialSimulationData.TransitionIfNeeded( BIT_HIGH );
	mSerialSimulationData.Advance( samples_per_bit * 2 );

	//there is no start bit on the break field.
	//// start bit...
	//mSerialSimulationData.Transition( ); 				//low-going edge for start bit
	//mSerialSimulationData.Advance( samples_per_bit );	//add start bit time

	U16 mask_byte = byte1;
	mask_byte |= ( ( U16 ) byte2 << 8 );
	U16 mask = 0x1 << 7;
	U32 accumulator = 0;
	for( U32 i = 0; i<13; i++ )
	{
		if( ( mask_byte & mask ) != 0 )
			mSerialSimulationData.TransitionIfNeeded( BIT_HIGH );
		else
			mSerialSimulationData.TransitionIfNeeded( BIT_LOW );

		mSerialSimulationData.Advance( samples_per_bit );
		mask = mask >> 1;
	}

	// stop bit...
	mSerialSimulationData.TransitionIfNeeded( BIT_HIGH );
	mSerialSimulationData.Advance( samples_per_bit * 2 );
}

void LINSimulationDataGenerator::CreateSyncField()
{
	CreateSerialByte( 0x55 );			// The sync byte field.
}

void LINSimulationDataGenerator::CreateProtectedIdentifierField(U8 id)
{
	U8 p0 = (id&1) ^ ((id>>1)&1) ^ ((id>>2)&1) ^ ((id>>4)&1);		// P0 Parity
	U8 p1 = ((id>>1)&1) ^ ((id>>3)&1) ^ ((id>>4)&1) ^ ((id>>5)&1); 	// P1 Parity
	CreateSerialByte( (id&0x3F) | ((p0&1)<<6) | ((p1&1)<<7) );
}

void LINSimulationDataGenerator::CreateSerialByte(U8 byte)
{
	U32 samples_per_bit = mSimulationSampleRateHz / mSettings->mBitRate;

	mChecksum.add(byte);
	SwapEnds(byte);

	// inter-byte space.....
	mSerialSimulationData.TransitionIfNeeded( BIT_HIGH );
	mSerialSimulationData.Advance( samples_per_bit * 2);

	// start bit...
	mSerialSimulationData.Transition(); 				//low-going edge for start bit
	mSerialSimulationData.Advance( samples_per_bit );	//add start bit time

	U8 mask = 0x1 << 7;
	for( U32 i=0; i<8; i++ )
	{
		if( ( byte & mask ) != 0 )
			mSerialSimulationData.TransitionIfNeeded( BIT_HIGH );
		else
			mSerialSimulationData.TransitionIfNeeded( BIT_LOW );

		mSerialSimulationData.Advance( samples_per_bit );
		mask = mask >> 1;
	}

	// stop bit...
	mSerialSimulationData.TransitionIfNeeded( BIT_HIGH );
	mSerialSimulationData.Advance( samples_per_bit * 2);
}

void LINSimulationDataGenerator::SwapEnds(U8& byte)
{
	U8 t=0;
	for(int n=7; n >= 0; n-- )
	{
		t |= ((byte>>n)&1) << 7;
		if ( n )
			t >>= 1;
	}
	byte = t;
}

U32 LINSimulationDataGenerator::Random( U32 min, U32 max )
{
	return min + ( rand() % ( max - min + 1 ) );


	U32 rc = min + (int) ( (double)max * (rand() / (RAND_MAX + 1.0)) );
	return rc;
}

