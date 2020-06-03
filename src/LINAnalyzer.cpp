#include "LINAnalyzer.h"
#include "LINAnalyzerSettings.h"
#include <AnalyzerChannelData.h>
#include <math.h>
#include <unordered_map>
namespace
{
    std::unordered_map<LINAnalyzerResults::tLINFrameState, std::string> FrameTypeStringLookup = {
        { LINAnalyzerResults::NoFrame, "NoFrame" },           { LINAnalyzerResults::headerBreak, "HeaderBreak" },
        { LINAnalyzerResults::headerSync, "HeaderSync" },     { LINAnalyzerResults::headerPID, "HeaderPID" },
        { LINAnalyzerResults::responseDataZero, "Data" },     { LINAnalyzerResults::responseData, "Data" },
        { LINAnalyzerResults::responseChecksum, "Checksum" }, { LINAnalyzerResults::responsePotentialChecksum, "DataOrChecksum" },
    };

    std::string FrameTypeToString( LINAnalyzerResults::tLINFrameState state )
    {
        return FrameTypeStringLookup.at( state );
    }

    std::vector<std::string> FrameFlagsToString( U8 flags )
    {
        std::vector<std::string> strings;
        if( flags & LINAnalyzerResults::byteFramingError )
            strings.push_back( "byteFramingError" );
        if( flags & LINAnalyzerResults::headerBreakExpected )
            strings.push_back( "headerBreakExpected" );
        if( flags & LINAnalyzerResults::headerSyncExpected )
            strings.push_back( "headerSyncExpected" );
        if( flags & LINAnalyzerResults::checksumMismatch )
            strings.push_back( "checksumMismatch" );

        return strings;
    }
}

LINAnalyzer::LINAnalyzer()
    : Analyzer2(), mSettings( new LINAnalyzerSettings() ), mSimulationInitilized( false ), mFrameState( LINAnalyzerResults::NoFrame )
{
    SetAnalyzerSettings( mSettings.get() );
}

LINAnalyzer::~LINAnalyzer()
{
    KillThread();
}

void LINAnalyzer::WorkerThread()
{
    mFrameState = LINAnalyzerResults::NoFrame; // reset every time we run.
    bool showIBS = false;                      // show inter-byte space?
    U8 nDataBytes = 0;
    bool byteFramingError;
    Frame byteFrame; // byte fame from start bit to stop bit
    Frame ibsFrame;  // inter-byte space startsing after SYNC
    bool is_data_really_break;
    bool ready_to_save = false;
    bool is_start_of_packet = false;

    ibsFrame.mData1 = 0;
    ibsFrame.mData2 = 0;
    ibsFrame.mFlags = 0;
    ibsFrame.mType = 0;

    mSerial = GetAnalyzerChannelData( mSettings->mInputChannel );

    if( mSerial->GetBitState() == BIT_LOW )
        mSerial->AdvanceToNextEdge();

    mResults->CancelPacketAndStartNewPacket();

    for( ;; )
    {
        is_data_really_break = false;
        is_start_of_packet = false;

        ibsFrame.mStartingSampleInclusive = mSerial->GetSampleNumber();
        if( ( mFrameState == LINAnalyzerResults::NoFrame ) || ( mFrameState == LINAnalyzerResults::headerBreak ) )
        {
            byteFrame.mData1 = GetBreakField( byteFrame.mStartingSampleInclusive, byteFrame.mEndingSampleInclusive, byteFramingError );
        }
        else
        {
            byteFrame.mData1 =
                ByteFrame( byteFrame.mStartingSampleInclusive, byteFrame.mEndingSampleInclusive, byteFramingError, is_data_really_break );
        }

        ibsFrame.mEndingSampleInclusive = byteFrame.mStartingSampleInclusive;
        byteFrame.mData2 = 0;
        byteFrame.mFlags = byteFramingError ? LINAnalyzerResults::byteFramingError : 0;
        byteFrame.mType = mFrameState;


        if( is_data_really_break )
        {
            // here we reset.
            mFrameState = LINAnalyzerResults::NoFrame;
            showIBS = false;
        }

        if( showIBS )
        {
            mResults->AddFrame( ibsFrame );
            // FrameV2 frame_v2_ibs;
            // mResults->AddFrameV2( frame_v2_ibs, "InterByteSpace", ibsFrame.mStartingSampleInclusive, ibsFrame.mEndingSampleInclusive );
        }
        ready_to_save = false;

        switch( mFrameState )
        {
        case LINAnalyzerResults::NoFrame:
            mFrameState = LINAnalyzerResults::headerBreak;
        case LINAnalyzerResults::headerBreak: // expecting break
            showIBS = true;
            if( byteFrame.mData1 == 0x00 )
            {
                mFrameState = LINAnalyzerResults::headerSync;
                byteFrame.mType = LINAnalyzerResults::headerBreak;
                is_start_of_packet = true;
            }
            else
            {
                byteFrame.mFlags |= LINAnalyzerResults::headerBreakExpected;
                mFrameState = LINAnalyzerResults::NoFrame;
            }
            break;
        case LINAnalyzerResults::headerSync: // expecting sync.
            if( byteFrame.mData1 == 0x55 )
            {
                mFrameState = LINAnalyzerResults::headerPID;
            }
            else
            {
                byteFrame.mFlags |= LINAnalyzerResults::headerSyncExpected;
                mFrameState = LINAnalyzerResults::NoFrame;
            }
            break;
        case LINAnalyzerResults::headerPID: // expecting PID.
        {
            mFrameState = LINAnalyzerResults::responseDataZero;

            bool classic_identifier = false;
            U8 identifier = byteFrame.mData1 & 0x3F;
            if( identifier == 0x3C || identifier == 0x3D )
                classic_identifier == true;

            mChecksum.clear();
            if( mSettings->mLINVersion >= 2 && classic_identifier == false )
            {
                mChecksum.add( byteFrame.mData1 ); // We only add the PID byte to the checksum IF we're using version 2 AND we're not using
                                                   // one of the classic identifers.
            }
        }
        break;
        // LIN Response
        case LINAnalyzerResults::responseDataZero: // expecting first resppnse data byte.
            if( mSettings->mLINVersion < 2 )
            {
                mChecksum.clear();
            }
            mChecksum.add( byteFrame.mData1 );
            nDataBytes = 1;
            mFrameState = LINAnalyzerResults::responseData;
            break;
        case LINAnalyzerResults::responseData: // expecting response data.
            if( nDataBytes >= 8 || mChecksum.result() == byteFrame.mData1 )
            {
                // FIXME - peek ahead for BREAK such that checksum match + BREAK detected at next char == end of packet.
                mFrameState = LINAnalyzerResults::responseChecksum;
                byteFrame.mType = LINAnalyzerResults::responseChecksum;
                // note, there is no break here. it rolls into the checksum state.

                if( nDataBytes >= 8 )
                    ready_to_save = true; // we should not terminate a packet unless we're sure that the packet is over, and we're not sure
                                          // yet, except when there are 8 data bytes.
                else
                    byteFrame.mType = LINAnalyzerResults::responsePotentialChecksum;
            }
            else
            {
                ++nDataBytes;
                mChecksum.add( byteFrame.mData1 );
                break;
            }
        case LINAnalyzerResults::responseChecksum: // expecting checksum.

            if( mChecksum.result() != byteFrame.mData1 )
            {
                byteFrame.mFlags |= LINAnalyzerResults::checksumMismatch;
            }
            if( ready_to_save == true )
            {
                mFrameState = LINAnalyzerResults::NoFrame;
                showIBS = false;
                nDataBytes = 0;
            }
            else
            {
                mFrameState = LINAnalyzerResults::responseData;
                mChecksum.add( byteFrame.mData1 );
                ++nDataBytes;
            }


            break;
        }

        byteFrame.mData2 = nDataBytes;

        if( is_start_of_packet )
            mResults->CommitPacketAndStartNewPacket(); // there is no harm in calling this more than once when no frames are commited.


        mResults->AddFrame( byteFrame );
        FrameV2 frame_v2;
        switch( static_cast<LINAnalyzerResults::tLINFrameState>( byteFrame.mType ) )
        {
        case LINAnalyzerResults::headerPID:
            frame_v2.AddInteger( "ProtectedId", byteFrame.mData1 & 0x3F );
            break;
        case LINAnalyzerResults::responseDataZero: // expecting first response data byte.
        case LINAnalyzerResults::responseData:     // expecting response data.
            frame_v2.AddInteger( "data", byteFrame.mData1 );
            frame_v2.AddInteger( "index", byteFrame.mData2 - 1 );
            break;
        case LINAnalyzerResults::responseChecksum: // expecting checksum.
            frame_v2.AddInteger( "checksum", byteFrame.mData1 );
            break;
        case LINAnalyzerResults::responsePotentialChecksum:
            // TODO: handle "Possible checksum" case!
            // I think LIN 2.0 explicitly specifies the message length based on the ID, removing any ambiguity.
            // for now, assume it's the checksum. We have already validated that the byte value is a valid checksum.
            frame_v2.AddInteger( "checksum", byteFrame.mData1 );
            frame_v2.AddInteger( "data", byteFrame.mData1 );
            frame_v2.AddInteger( "index", byteFrame.mData2 - 1 );
            break;
        default:
            break;
        }
        auto flag_strings = FrameFlagsToString( byteFrame.mFlags );
        for( const auto& flag_string : flag_strings )
        {
            frame_v2.AddBoolean( flag_string.c_str(), true );
        }
        mResults->AddFrameV2( frame_v2, FrameTypeToString( static_cast<LINAnalyzerResults::tLINFrameState>( byteFrame.mType ) ).c_str(),
                              byteFrame.mStartingSampleInclusive, byteFrame.mEndingSampleInclusive );

        if( ready_to_save )
            mResults->CommitPacketAndStartNewPacket();
        mResults->CommitResults();
        ReportProgress( byteFrame.mEndingSampleInclusive );
    }
}

bool LINAnalyzer::NeedsRerun()
{
    return false;
}

U32 LINAnalyzer::GenerateSimulationData( U64 minimum_sample_index, U32 device_sample_rate,
                                         SimulationChannelDescriptor** simulation_channels )
{
    if( mSimulationInitilized == false )
    {
        mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), mSettings.get() );
        mSimulationInitilized = true;
    }

    return mSimulationDataGenerator.GenerateSimulationData( minimum_sample_index, device_sample_rate, simulation_channels );
}

U32 LINAnalyzer::GetMinimumSampleRateHz()
{
    return mSettings->mBitRate * 4;
}

void LINAnalyzer::SetupResults()
{
    mResults.reset( new LINAnalyzerResults( this, mSettings.get() ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings->mInputChannel );
}

const char* LINAnalyzer::GetAnalyzerName() const
{
    return "LIN";
}

const char* GetAnalyzerName()
{
    return "LIN";
}

Analyzer* CreateAnalyzer()
{
    return new LINAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
    delete analyzer;
}

double LINAnalyzer::SamplesPerBit()
{
    return ( double )GetSampleRate() / ( double )mSettings->mBitRate;
}

double LINAnalyzer::HalfSamplesPerBit()
{
    return SamplesPerBit() * 0.5;
}

void LINAnalyzer::Advance( U16 nBits )
{
    mSerial->Advance( nBits * SamplesPerBit() );
}

void LINAnalyzer::AdvanceHalfBit()
{
    mSerial->Advance( HalfSamplesPerBit() );
}

U8 LINAnalyzer::GetBreakField( S64& startingSample, S64& endingSample, bool& framingError )
{
    // locate the start bit (falling edge expected)...
    U32 min_break_field_low_bits =
        13; // as per the spec of LIN. at least 13 bits at master speed, but receiver only needs it to be 11 or more.
    U32 num_break_bits = 0;
    bool valid_fame = false;
    for( ;; )
    {
        mSerial->AdvanceToNextEdge();
        if( mSerial->GetBitState() == BIT_HIGH )
        {
            mSerial->AdvanceToNextEdge();
        }
        num_break_bits = round( double( mSerial->GetSampleOfNextEdge() - mSerial->GetSampleNumber() ) / SamplesPerBit() );
        if( num_break_bits >= min_break_field_low_bits )
        {
            startingSample = mSerial->GetSampleNumber();
            valid_fame = true;
            break;
        }
    }

    // AdvanceHalfBit( );
    // mResults->AddMarker( mSerial->GetSampleNumber( ), AnalyzerResults::Start, mSettings->mInputChannel );

    for( U32 i = 0; i < num_break_bits; i++ )
    {
        if( i == 0 )
            AdvanceHalfBit();
        else
            Advance( 1 );
        // let's put a dot exactly where we sample this bit:
        mResults->AddMarker( mSerial->GetSampleNumber(), mSerial->GetBitState() == BIT_HIGH ? AnalyzerResults::One : AnalyzerResults::Zero,
                             mSettings->mInputChannel );
    }

    // Validate the stop bit...
    Advance( 1 );
    if( mSerial->GetBitState() == BIT_HIGH )
    {
        mResults->AddMarker( mSerial->GetSampleNumber(), AnalyzerResults::Stop, mSettings->mInputChannel );
        framingError = false;
    }
    else
    {
        mResults->AddMarker( mSerial->GetSampleNumber(), AnalyzerResults::ErrorSquare, mSettings->mInputChannel );
        framingError = true;
    }

    endingSample = mSerial->GetSampleNumber();

    return ( valid_fame ) ? 0 : 1;
}

U8 LINAnalyzer::ByteFrame( S64& startingSample, S64& endingSample, bool& framingError, bool& is_break_field )
{
    U8 data = 0;
    U8 mask = 1;

    framingError = false;
    is_break_field = false;

    // locate the start bit (falling edge expected)...
    mSerial->AdvanceToNextEdge();
    if( mSerial->GetBitState() == BIT_HIGH )
    {
        AdvanceHalfBit();
        mResults->AddMarker( mSerial->GetSampleNumber(), AnalyzerResults::ErrorDot, mSettings->mInputChannel );
        mSerial->AdvanceToNextEdge();
        // framingError = true;
    }
    startingSample = mSerial->GetSampleNumber();
    AdvanceHalfBit();
    mResults->AddMarker( mSerial->GetSampleNumber(), AnalyzerResults::Start, mSettings->mInputChannel );

    // mark each data bit (LSB first)...
    for( U32 i = 0; i < 8; i++ )
    {
        Advance( 1 );

        if( mSerial->GetBitState() == BIT_HIGH )
            data |= mask;

        // let's put a dot exactly where we sample this bit:
        mResults->AddMarker( mSerial->GetSampleNumber(), mSerial->GetBitState() == BIT_HIGH ? AnalyzerResults::One : AnalyzerResults::Zero,
                             mSettings->mInputChannel );

        mask = mask << 1;
    }

    // Validate the stop bit...
    Advance( 1 );
    if( mSerial->GetBitState() == BIT_HIGH )
    {
        mResults->AddMarker( mSerial->GetSampleNumber(), AnalyzerResults::Stop, mSettings->mInputChannel );
    }
    else
    {
        // check to see if we're really in a break frame...
        // we should be dead center in the stop bit here. (10 bits in, exactly)
        // a break frame has at least 13 bits low, however the slave is only required to measure 11 bits low.
        // there is no maximum limit to the length of the break frame.

        bool all_13_clear =
            !( mSerial->WouldAdvancingCauseTransition( SamplesPerBit() * 3 ) ); // checks the remaining 3 bits to make sure they are high.

        mSerial->AdvanceToNextEdge(); // we're high again, and at the end of the break frame!
        // verify that we've found a stop bit.
        bool high_bit_present = !mSerial->WouldAdvancingCauseTransition(
            SamplesPerBit() * 0.5 ); // if there are no transitions, that means it's high in the center of the bit field, and we're good!


        // bool high_bit_present = mSerial->WouldAdvancingCauseTransition( SamplesPerBit() * 4 );

        if( all_13_clear && high_bit_present )
        {
            /*mResults->AddMarker( mSerial->GetSampleNumber(), mSerial->GetBitState() == BIT_HIGH ? AnalyzerResults::One :
            AnalyzerResults::Zero, mSettings->mInputChannel ); for( U32 i = 0; i < 4; ++i )
            {
                Advance( 1 );
                mResults->AddMarker( mSerial->GetSampleNumber(), mSerial->GetBitState() == BIT_HIGH ? AnalyzerResults::One :
            AnalyzerResults::Zero, mSettings->mInputChannel );
            }*/

            endingSample = mSerial->GetSampleNumber();
            is_break_field = true;
            return 0x00;
        }


        mResults->AddMarker( mSerial->GetSampleNumber(), AnalyzerResults::ErrorSquare, mSettings->mInputChannel );
        framingError = true;
    }

    endingSample = mSerial->GetSampleNumber();

    return data;
}
