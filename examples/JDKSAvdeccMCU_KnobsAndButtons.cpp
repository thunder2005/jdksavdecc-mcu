#if defined( __APPLE__ ) || defined( __linux__ )
#define JDKSAVDECCMCU_ARDUINO 1
#define JDKSAVDDECC_ARDUINO_FAKE_IMPL 1
#else
#include <SPI.h>
#include <Ethernet.h>
#endif

#include "JDKSAvdeccMCU_Arduino.h"

#define REFRESH_TIME ( 1000 )

using namespace JDKSAvdeccMCU;

/// This MAC address is based on the J.D. Koftinoff Software, Ltd. assigned
/// MAC-S (OUI36): 70:b3:d5:ed:c
/// JDKS reserves the MAC range from 70:b3:d5:ed:cf:f0 to 70:b3:d5:ed:cf:f7
/// inclusive for experimental devices only

jdksavdecc_eui48 my_mac = {{0x70, 0xb3, 0xd5, 0xed, 0xcf, 0xf0}};

/// Ethernet handler object

#if JDKSAVDECCMCU_ENABLE_RAWSOCKETWIZNET == 1
// for embedded systems using the WizNet ethernet chip
RawSocketWizNet rawnet( my_mac,
                        JDKSAVDECC_AVTP_ETHERTYPE,
                        &jdksavdecc_multicast_adp_acmp );
#elif JDKSAVDECCMCU_ENABLE_RAWSOCKETPCAPFILE == 1
// for unit tests using pcap files for input and output
RawSocketPcapFile rawnet( JDKSAVDECC_AVTP_ETHERTYPE,
                          my_mac,
                          jdksavdecc_multicast_adp_acmp,
                          &jdksavdecc_multicast_adp_acmp,
                          "input.pcap",
                          "output.pcap",
                          50 );
#elif JDKSAVDECCMCU_ENABLE_RAWSOCKETLINUX == 1
// for Raw sockets on linux
RawSocketLinux rawnet( "en0",
                       JDKSAVDECC_AVTP_ETHERTYPE,
                       &jdksavdecc_multicast_adp_acmp );
#elif JDKSAVDECCMCU_ENABLE_RAWSOCKETMACOSX == 1

// for Raw sockets on Mac OSX
RawSocketMacOSX rawnet( "en0",
                        JDKSAVDECC_AVTP_ETHERTYPE,
                        &jdksavdecc_multicast_adp_acmp );
#endif

/// This AVDECC Entity is based on the mac address (insert ff ff)
jdksavdecc_eui64 my_entity_id
    = {{0x70, 0xb3, 0xd5, 0xff, 0xff, 0xed, 0xcf, 0xf0}};

/// This AVDECC Entity Model ID is based on the J.D. Koftinoff Software, Ltd.
/// assigned MAC-S (OUI36): 70:b3:d5:ed:c
jdksavdecc_eui64 my_entity_model_id
    = {{0x70, 0xb3, 0xd5, 0xed, 0xc0, 0x00, 0x00, 0x00}};

/// All of the controls are targetting the entity of the second JDKS
/// Experimental MAC address
jdksavdecc_eui64 target_entity_id
    = {{0x70, 0xb3, 0xd5, 0xff, 0xff, 0xed, 0xcf, 0xf1}};

/// All of the controls are targetting the second JDKS experimental MAC address
/// 70:b3:d5:ed:cf:f1
jdksavdecc_eui48 target_mac_address = {{0x70, 0xb3, 0xd5, 0xed, 0xcf, 0xf1}};

class KnobsAndButtonsController : public EntityState
{
  public:
    KnobsAndButtonsController( jdksavdecc_eui64 const &entity_id,
                               jdksavdecc_eui64 const &entity_model_id )
        : m_adp_manager( rawnet,
                         entity_id,
                         entity_model_id,
                         JDKSAVDECC_ADP_ENTITY_CAPABILITY_AEM_SUPPORTED,
                         JDKSAVDECC_ADP_CONTROLLER_CAPABILITY_IMPLEMENTED,
                         50 )
        , m_controller_entity( m_adp_manager, this )
        , m_update_rate_in_millis( 50 )
        , m_last_update_time( 0 )
        , m_knobs_sender( m_controller_entity,
                          target_entity_id,
                          target_mac_address,
                          0x0000,
                          REFRESH_TIME,
                          &m_knobs_storage )
        , m_buttons_sender( m_controller_entity,
                            target_entity_id,
                            target_mac_address,
                            0x0001,
                            REFRESH_TIME,
                            &m_buttons_storage )
    {
        // Set up the I/O pins for 3 knobs, 5 buttons, and 2 LED's
        pinMode( 9, OUTPUT );
        pinMode( 2, INPUT_PULLUP );
        pinMode( 3, INPUT_PULLUP );
        pinMode( 4, INPUT_PULLUP );
        pinMode( 5, INPUT_PULLUP );
        pinMode( 6, INPUT_PULLUP );
        pinMode( A4, OUTPUT );
        pinMode( A5, OUTPUT );
    }

    virtual void addToHandlerGroup( HandlerGroup &group )
    {
        group.add( &m_adp_manager );
        group.add( &m_knobs_sender );
        group.add( &m_buttons_sender );
        group.add( &m_controller_entity );
        group.add( this );
    }

    virtual void tick( jdksavdecc_timestamp_in_milliseconds time_in_millis )
    {
        if ( wasTimeOutHit(
                 time_in_millis, m_last_update_time, m_update_rate_in_millis ) )
        {
            m_last_update_time = time_in_millis;

            for ( uint8_t i = 0; m_knobs_storage.getNumItems(); ++i )
            {
                m_knobs_storage.setDoublet( analogRead( i ), i );
            }

            for ( uint8_t i = 0; m_buttons_storage.getNumItems(); ++i )
            {
                m_buttons_storage.setDoublet(
                    digitalRead( i + 2 ) ? 0x00 : 0xff, i );
            }

            digitalWrite( A5, !digitalRead( 2 ) );
            digitalWrite( A4, !digitalRead( 3 ) );
        }
    }

    virtual uint8_t readDescriptorEntity( Frame &pdu,
                                          uint16_t configuration_index,
                                          uint16_t descriptor_index )
    {
        uint8_t status = JDKSAVDECC_AEM_STATUS_BAD_ARGUMENTS;

        if ( configuration_index == 0 && descriptor_index == 0 )
        {
            status = fillDescriptorEntity(
                pdu,
                m_controller_entity.getEntityID(),
                m_adp_manager.getEntityModelID(),
                JDKSAVDECC_ADP_ENTITY_CAPABILITY_AEM_SUPPORTED,
                0,
                0,
                0,
                0,
                JDKSAVDECC_ADP_CONTROLLER_CAPABILITY_IMPLEMENTED,
                m_adp_manager.getAvailableIndex(),
                "JDKSAvdecc-MCU",
                JDKSAVDECC_NO_STRING,
                JDKSAVDECC_NO_STRING,
                "V2.0",
                "",
                "12345678",
                1,
                0 );

            if ( status == JDKSAVDECC_AEM_STATUS_SUCCESS )
            {
                m_controller_entity.sendResponses( false, false, pdu );
            }
        }

        return status;
    }

    virtual uint8_t readDescriptorAvbInterface( Frame &pdu,
                                                uint16_t configuration_index,
                                                uint16_t descriptor_index )
    {
        uint8_t status = JDKSAVDECC_AEM_STATUS_BAD_ARGUMENTS;

        if ( configuration_index == 0 && descriptor_index == 0 )
        {
            pdu.setLength(
                JDKSAVDECC_FRAME_HEADER_LEN
                + JDKSAVDECC_AEM_COMMAND_READ_DESCRIPTOR_COMMAND_LEN );

            // object_name
            pdu.putAvdeccString();

            // localized_description
            pdu.putDoublet( JDKSAVDECC_NO_STRING );

            // mac_address
            pdu.putEUI48( m_controller_entity.getRawSocket().getMACAddress() );

            // interface_flags
            pdu.putDoublet( 0 );

            // clock_identity
            pdu.putEUI64();

            // priority1
            pdu.putOctet( 0xff );

            // clock_class
            pdu.putOctet( 0xff );

            // offset_scaled_log_variance
            pdu.putDoublet( 0 );

            // clock_accuracy
            pdu.putOctet( 0 );

            // priority2
            pdu.putOctet( 0xff );

            // domain_number
            pdu.putOctet( 0 );

            // log_sync_interval
            pdu.putOctet( 0 );

            // log_announce_interval
            pdu.putOctet( 0 );

            // log_pdelay_interval
            pdu.putOctet( 0 );

            // port_number
            pdu.putOctet( 0 );

            m_controller_entity.sendResponses( false, false, pdu );
            status = JDKSAVDECC_AEM_STATUS_SUCCESS;
        }
        return status;
    }

    virtual uint8_t readDescriptorConfiguration( Frame &pdu,
                                                 uint16_t configuration_index,
                                                 uint16_t descriptor_index )
    {
        uint8_t status = JDKSAVDECC_AEM_STATUS_BAD_ARGUMENTS;

        if ( configuration_index == 0 && descriptor_index == 0 )
        {
            pdu.setLength(
                JDKSAVDECC_FRAME_HEADER_LEN
                + JDKSAVDECC_AEM_COMMAND_READ_DESCRIPTOR_COMMAND_LEN );

            // object_name
            pdu.putAvdeccString();

            // localized_description
            pdu.putDoublet( JDKSAVDECC_NO_STRING );

            // descriptor_counts_count
            pdu.putDoublet( 2 );

            // descriptor_counts_offset
            pdu.putDoublet( JDKSAVDECC_DESCRIPTOR_CONFIGURATION_LEN );

            // descriptor type
            pdu.putDoublet( JDKSAVDECC_DESCRIPTOR_AVB_INTERFACE );
            // descriptor count
            pdu.putDoublet( 1 );

            // descriptor type
            pdu.putDoublet( JDKSAVDECC_DESCRIPTOR_CONTROL );
            // descriptor count
            pdu.putDoublet( 2 );

            m_controller_entity.sendResponses( false, false, pdu );
            status = JDKSAVDECC_AEM_STATUS_SUCCESS;
        }
        return status;
    }

    virtual uint8_t readDescriptorControl( Frame &pdu,
                                           uint16_t configuration_index,
                                           uint16_t descriptor_index )
    {
        uint8_t status = JDKSAVDECC_AEM_STATUS_BAD_ARGUMENTS;

        if ( configuration_index == 0 && descriptor_index < 2 )
        {
            pdu.setLength(
                JDKSAVDECC_FRAME_HEADER_LEN
                + JDKSAVDECC_AEM_COMMAND_READ_DESCRIPTOR_COMMAND_LEN );

            // object_name
            pdu.putAvdeccString();

            // localized_description
            pdu.putDoublet( JDKSAVDECC_NO_STRING );

            // block_latency
            pdu.putQuadlet( 0 );

            // control_latency
            pdu.putQuadlet( 50000 );

            // control_domain
            pdu.putDoublet( 0 );

            if ( descriptor_index == 0 )
            {
                // control_value_type
                pdu.putDoublet( JDKSAVDECC_CONTROL_VALUE_ARRAY_UINT16 );

                // control_type
                pdu.putEUI64( 0x70, 0xb3, 0xd5, 0xed, 0xcf, 0x00, 0x00, 0x00 );
            }
            if ( descriptor_index == 1 )
            {
                // control_value_type
                pdu.putDoublet( JDKSAVDECC_CONTROL_VALUE_ARRAY_UINT8 );

                // control_type
                pdu.putEUI64( 0x70, 0xb3, 0xd5, 0xed, 0xcf, 0x00, 0x00, 0x01 );
            }

            // reset_time
            pdu.putQuadlet( 0 );

            // values_offset
            pdu.putDoublet( JDKSAVDECC_DESCRIPTOR_CONTROL_LEN );

            if ( descriptor_index == 0 )
            {
                // number_of_values
                pdu.putDoublet( m_knobs_storage.getNumItems() );
            }

            if ( descriptor_index == 1 )
            {
                // number_of_values
                pdu.putDoublet( m_buttons_storage.getNumItems() );
            }

            // signal_type
            pdu.putDoublet( JDKSAVDECC_DESCRIPTOR_ENTITY );

            // signal_index
            pdu.putDoublet( 0 );

            // signal_output
            pdu.putDoublet( 0 );

            if ( descriptor_index == 0 )
            {
                // minimum
                pdu.putDoublet( 0 );

                // maximium
                pdu.putDoublet( 0x1fff );

                // step
                pdu.putDoublet( 0x80 );

                // default
                pdu.putDoublet( 0 );

                // unit multiplier 10^0
                pdu.putOctet( 0 );

                // unit code (unitless)
                pdu.putOctet( JDKSAVDECC_AEM_UNIT_UNITLESS );

                // string
                pdu.putDoublet( JDKSAVDECC_NO_STRING );

                // current values
                for ( uint8_t i = 0; i < m_knobs_storage.getNumItems(); ++i )
                {
                    pdu.putDoublet( m_knobs_storage.getDoublet( i ) );
                }
            }
            if ( descriptor_index == 1 )
            {
                // minimum
                pdu.putOctet( 0 );

                // maximium
                pdu.putOctet( 0xff );

                // step
                pdu.putOctet( 0x80 );

                // default
                pdu.putOctet( 0 );

                // unit multiplier 10^0
                pdu.putOctet( 0 );

                // unit code (unitless)
                pdu.putOctet( JDKSAVDECC_AEM_UNIT_UNITLESS );

                // string
                pdu.putDoublet( JDKSAVDECC_NO_STRING );

                // current values
                for ( uint8_t i = 0; i < m_buttons_storage.getNumItems(); ++i )
                {
                    pdu.putOctet( m_buttons_storage.getOctet( i ) );
                }
            }
            m_controller_entity.sendResponses( false, false, pdu );
            status = JDKSAVDECC_AEM_STATUS_SUCCESS;
        }
        return status;
    }

    virtual uint8_t receiveGetControlCommand( Frame &pdu,
                                              uint16_t descriptor_index )
    {
        uint8_t status = JDKSAVDECC_AEM_STATUS_BAD_ARGUMENTS;

        if ( descriptor_index < 2 )
        {
            pdu.setLength( JDKSAVDECC_FRAME_HEADER_LEN
                           + JDKSAVDECC_AEM_COMMAND_GET_CONTROL_COMMAND_LEN );

            if ( descriptor_index == 0 )
            {
                // current values
                for ( uint8_t i = 0; i < m_knobs_storage.getNumItems(); ++i )
                {
                    pdu.putDoublet( m_knobs_storage.getDoublet( i ) );
                }
            }

            if ( descriptor_index == 1 )
            {
                // current values
                for ( uint8_t i = 0; i < m_buttons_storage.getNumItems(); ++i )
                {
                    pdu.putOctet( m_buttons_storage.getOctet( i ) );
                }
            }

            m_controller_entity.sendResponses( false, false, pdu );
            status = JDKSAVDECC_AEM_STATUS_SUCCESS;
        }
        return status;
    }

  private:
    /// The ADP manager is told about the entity id, model_id, entity
    /// capabilities, controller capabilities, and valid time in
    /// seconds
    ADPManager m_adp_manager;

    ControllerEntity m_controller_entity;

    /// The update rate in milliseconds
    uint32_t m_update_rate_in_millis;

    /// The time that the last update happened
    uint32_t m_last_update_time;

    /// The mapping of Knob 1,2,3 to control descriptor 0x0000. 6 byte payload,
    /// one doublet each
    ControlValueHolderWithStorage<uint16_t, 3> m_knobs_storage;
    ControlSender m_knobs_sender;

    /// The mapping of Button 1 to control descriptor 0x0001. 5 byte payload,
    /// one octet each
    ControlValueHolderWithStorage<uint8_t, 5> m_buttons_storage;
    ControlSender m_buttons_sender;
};

KnobsAndButtonsController my_entity_state( my_entity_id, my_entity_model_id );

/// Create a HandlerGroup which can manage up to 16 handlers
HandlerGroupWithSize<16> all_handlers;

void setup()
{

    // Initialize the ethernet chip
    rawnet.initialize();

    // Put all the handlers into the HandlerGroup

    my_entity_state.addToHandlerGroup( all_handlers );
}

void jdksavdeccmcu_debug_log( const char *str, uint16_t v )
{
    static uint16_t debug_sequence_id = 0;
    char txt[64];
    FrameWithSize<192> pdu;
    uint16_t r;
    sprintf( txt, "%s %u", str, (unsigned)v );
    r = jdksavdecc_jdks_log_control_generate( &my_entity_id,
                                              8, // Control index 8
                                              &debug_sequence_id,
                                              JDKSAVDECC_JDKS_LOG_INFO,
                                              0,
                                              txt,
                                              pdu.getBuf(),
                                              pdu.getMaxLength() );
    if ( r > 0 )
    {
        pdu.setLength( r );
        memcpy(
            pdu.getBuf( JDKSAVDECC_FRAME_HEADER_SA_OFFSET ), my_mac.value, 6 );
        RawSocket::multiSendFrame( pdu );
    }
}

void loop()
{
    static jdksavdecc_timestamp_in_milliseconds last_second = 0;
    // Get the current time in milliseconds
    jdksavdecc_timestamp_in_milliseconds cur_time
        = RawSocket::multiGetTimeInMilliseconds();

    // Tell all the handlers to do their periodic jobs
    all_handlers.tick( cur_time );
    if ( cur_time / 1000 != last_second )
    {
        last_second = cur_time / 1000;
        jdksavdeccmcu_debug_log( "Time from KnobsAndButtons is:", last_second );
    }
}

#ifndef __AVR__
int main( int argc, char **argv )
{
    (void)argc;
    (void)argv;
    setup();
    while ( true )
    {
        loop();
    }
}

#endif
