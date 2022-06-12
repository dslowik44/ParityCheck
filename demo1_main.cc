#include "parity_checking.hpp"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <random>
#include <algorithm>

using std::cout, std::endl, std::hex;

using ParityChecking::ParityHdr;           // class to store byte arrays' parity information.
using ParityChecking::repair_byte_array;   // function to repair byte error transmission errors.

unsigned char* transmit(const unsigned char*, size_t); // mimics transmission of byte array.
static double ERROR_RATE{ 0 };   // probability of any 1 bit flipping during transmition. Try 1./len_s.


int main() {
    /* ParityChecking functionality demonstration.
       class ParityHdr in ParityChecking namespace contains and points to all parity information
       required to do a good job confirming accurate transmission and repairing single bit errors
       in its corresponding byte array.

       Create a byte array, s, constructing its' corresponding ParityHdr, s_hdr.
       Simulates transmitting this s and the serialized header s_hdr_ser, with the function
       transmit(). transmit() will add random noise to a 'transmitted' byte array at a rate controlled
       by ERROR_RATE. After receipt of the byte array representing the ParityHdr, it is used to
       construct the received ParityHdr whose check_sum is confirmed and a ParityHdr is reconstructed 
       as a faithful representation of the original byte array s.

       array with a little noise added. s transmits to t, and s_hdr transmits to t_hdr.
       (Normally s and t will be in seperate processes, and transmit will actually send the byte
       array across some low noise communication network between them.)
    */

    // Construct byte array, s, to be transmitted:
  /*  const char* ss = "Hello -- world!";  // cstring for convenience but ParityChecking works with unsigned chars.
    unsigned int len_s = std::strlen(ss);
    const int B = 5, N = 3;              // Must choose B, N s.t. B*N == len_s.
    const unsigned char* s = reinterpret_cast<const unsigned char*>(ss);
    */

    const int B = 100, N = 100;  // len_s must factor into B * N for ParityHdr construction.
    const int len_s = B * N;
    unsigned char* s = new unsigned char[len_s];
    std::memset(s, 0xfe, B * N);

    ERROR_RATE = 2. / (8 * len_s);  // average 2 bit flips in transmission of s.

    // Construct its ParityHdr, s_hdr:
    ParityHdr s_hdr(B, N, s);

    // Serialize s_hdr for transmission:
    const unsigned char* s_hdr_ser = s_hdr.serialize();

    // Construct a ParityHdr to receive this:
    ParityHdr rcvd_hdr;

    // Keep sending s_hdr_ser until good(check_sum matches) receipt:
    bool check_sum_match = false;
    int n_transmits{ 1 };
    int MAX_HDR_TRYS{ 30 };
    while (n_transmits < MAX_HDR_TRYS) {
        //cout << "Transmitting ParityHdr, attempt #" << n_transmits << endl;
        size_t n_bytes = 3 * sizeof(unsigned int) + 4 * sizeof(short) + B + N;
        const unsigned char* rcvd_hdr_ser = transmit(s_hdr_ser, n_bytes);
        check_sum_match = rcvd_hdr.load_from_serialized(rcvd_hdr_ser);
        delete[] rcvd_hdr_ser;
        if (check_sum_match)
            break;
        ++n_transmits;
    }
    if (n_transmits >= MAX_HDR_TRYS) {
        cout << "Too many ParityHdr transmission attempts, Exiting" << endl;
        std::exit(1);
    }
    cout << "Received check_sum confirmed ParityHdr, rcvd_hdr.\n\n";
    if (rcvd_hdr != s_hdr)
        cout << "rcvd_hdr != s_hdr\n";

    // At this point we have a check_sum confirmed good receipt of s_hdr as rcvd_hdr.
    // Now transmit byte array s to t. rcvd_hdr will be used to check t for errors
    // and to correct up to 1 bit error in t:
    int MAX_TRYS = 30;
    int n_trys{ 0 };
    unsigned char* t{ nullptr };
    while (n_trys < MAX_TRYS) {
        // Transmit s to the receivers' byte array t:
        n_trys += 1;
        t = transmit(s, B * N);
        // and construct its' ParityHdr: (Note we use rcvd_hdr to get dimensions B and N) 
        ParityHdr t_hdr(rcvd_hdr.getB(), rcvd_hdr.getN(), t);

        // Compare this t_hdr with the already received and check_sum confirmed ParityHdr, rcvd_hdr:
        if (t_hdr == rcvd_hdr) {
            cout << "ParityHdrs match, No parity detectable errors during transmision\n";
            cout << "Received byte array: \n";
            for (int i = 0; i < std::min(t_hdr.getB() * t_hdr.getN(), 100U); ++i)
                cout << hex << static_cast<int>(t[i]) << ((i + 1) % 32 ? ' ' : '\n');
            cout << "..." << endl;
        }
        else {  // Use ParityHdrs to repair t:
            try {
                repair_byte_array(rcvd_hdr, t_hdr, t);
            }
            catch (ParityChecking::PC_Exception& e) {
                cout << "Error in repair_byte_array:\n";
                cout << e.what() << "-Retransmitting...\n" << endl;
                delete[] t;
                continue;
            }
            cout << "Repaired the received byte array to give:\n";
            for (int i = 0; i < std::min(t_hdr.getB() * t_hdr.getN(), 100U); ++i)
                cout << hex << static_cast<int>(t[i]) << ((i + 1) % 32 ? ' ' : '\n');
            cout << "..." << endl;
        }
        break;
    }
    if (n_trys >= MAX_TRYS) {
        cout << "Too many byte array transmission attempts, Exiting" << endl;
        std::exit(1);
    }

    // Print out any time the recovered byte array differed from that sent:
    for (int i = 0; i < B * N; ++i) {
        if (t[i] != s[i]) {
            cout << "t != s\n";
            break;
        }
    }

    delete[] s;
    delete[] s_hdr_ser;
    delete[] t;

    return 0; 
}

unsigned char* transmit(const unsigned char* cs, size_t len) {
    /* Used to simulate transmission of s and s_hdr_ser from the sending process
       over to t and rcvd_hdr_ser in the receiving process. new memory is allocated to
       receive the transmitted byte arrays and pointer to this new memory is returned.
       This allows to compare byte arrays pointed to by s and t within this single process
       simulation environment to look for differences -breakthrough events that have 
       subverted the whole "pretty safe" transmission protocol implemented here.

       cs is pointer to byte array to be transmitted.
       len == B*N is length of byte array in bytes(== 8*len bits).
       ERROR_RATE is the IID probability that any one transmitted bit is flipped.
       It is assumed any bit is flipped with this same probability, independent of
       any other bit flips.
       pointer, s, to new memory containing the received byte array is returned. */

    unsigned char* s = new unsigned char[len];
    std::memcpy(s, cs, len);

    if (ERROR_RATE < 1.e-9 / len) // neglect transmission errors (geom{0} returns garbage.)
        return s;

    // long unsigned int seed = 1;    // Replace next 2 lines with these 2 for reproducing random 
    // static std::mt19937 gen{seed}; // bit streams for testing.
    std::random_device rd;
    static std::mt19937 gen{ rd() };

    // idx increment to next flipped bit is geometrically distributed with probability of bit 
    // flip == ERROR_RATE:
    std::geometric_distribution<> geom{ ERROR_RATE };

    int idx{ 0 };                 // Starting at beginning of byte array,
    while (idx < 8 * len) {
        idx += geom(gen);         // random number of steps to next flip
        if (idx < 8 * len)        // if that lies within array being transmitted
            s[idx / 8] ^= 0x80 >> idx % 8;  // flip it.
    }

    return s;
}

