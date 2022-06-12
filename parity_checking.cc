#include "parity_checking.hpp"
#include <cstring>
#include <algorithm>

using std::min;


namespace ParityChecking {

    ParityHdr::ParityHdr() : check_sum{ 0 }, B{ 0 }, N{ 0 }, row_parities{ nullptr }, col_parities{ nullptr } { }
    ParityHdr::ParityHdr(unsigned short B, unsigned short N, const unsigned char* byte_array)
        /* length of byte_array == B * N, conceptualized as B rows, N cols of matrix
           of bytes whose row/col parities are stored in this ParityHdr. */
        : B{ B }, N{ N } {
        row_parities = new unsigned char[B];
        col_parities = new unsigned char[N];
        calculate_parities(byte_array); // fills in above 2 arrays.
        check_sum = calc_check_sum();
    }

    void ParityHdr::calculate_parities(const unsigned char* byte_array) {
        std::memset(row_parities, 0, B);
        std::memset(col_parities, 0, N);
        int L = B * N;
        for (int i = 0; i < L; ++i) {
            row_parities[i % B] ^= byte_array[i];
            col_parities[i / B] ^= byte_parity(byte_array[i]);
        }

        return;
    }

    const unsigned char* ParityHdr::serialize() const {  // Typically used before transmitting.
        /* return a byte array with all the information tracked by this ParityHdr. */
        int len = 3 * sizeof(int) + 4 * sizeof(short) + B + N;
        unsigned char* ser_PH = new unsigned char[len];
        std::memcpy(ser_PH, this, sizeof(int) + 2 * sizeof(short));
        std::memcpy(ser_PH + sizeof(int) + 2 * sizeof(short), this, sizeof(int) + 2 * sizeof(short));  // doubly copy critical info...
        // insert additional check that row_parities sum is preserved:
        unsigned int sum_row_parities{ 0 };
        for (int b = 0; b < B; ++b)
            sum_row_parities += row_parities[b];
        std::memcpy(ser_PH + 2 * sizeof(int) + 4 * sizeof(short), &sum_row_parities, sizeof(int));

        // Note: We're not copying the pointers, but the byte arrays pointed to:
        std::memcpy(ser_PH + 3 * sizeof(int) + 4 * sizeof(short), row_parities, B);
        std::memcpy(ser_PH + 3 * sizeof(int) + 4 * sizeof(short) + B, col_parities, N);
        return ser_PH;
    }

    bool ParityHdr::load_from_serialized(const unsigned char* ser_PH) {
        /* Used to load received byte array of ParityHdr info into an empty ParityHdr.
           returns bool indicating this load resulted in a good check_sum. */
           // first confirm check_sum, B and N are very probably good, since we will be accessing 
           // memory regions based on B and N below...
        if (std::memcmp(ser_PH, ser_PH + sizeof(int) + 2 * sizeof(short), sizeof(int) + 2*sizeof(short)) != 0)
            return false;
        std::memcpy(this, ser_PH, sizeof(int) + 2*sizeof(short));
        if (B + N > check_sum)
            return false;
        // and check row_parities sum was seperately preserved:
        unsigned int sum_row_parities{ 0 };
        for (int b = 0; b < B; ++b)
            sum_row_parities += ser_PH[3 * sizeof(int) + 4 * sizeof(short) + b];
        if (sum_row_parities != *reinterpret_cast<const unsigned int*>(ser_PH + 2 * sizeof(int) + 4*sizeof(short)))
            return false;
        delete[] row_parities;  // load_from_serialized may be called repetitively.
        delete[] col_parities;
        // Allocate heap memory to copy row/col_parities byte arrays into:
        row_parities = new unsigned char[B];
        std::memcpy(row_parities, ser_PH + 3 * sizeof(int) + 4 * sizeof(short), B);
        col_parities = new unsigned char[N];
        std::memcpy(col_parities, ser_PH + 3 * sizeof(int) + 4*sizeof(short) + B, N);
        return confirm_check_sum();
    }

    bool ParityHdr::confirm_check_sum() const {
        return check_sum == calc_check_sum();
    }

    unsigned int ParityHdr::calc_check_sum() const {
        unsigned int chk_sum{ static_cast<unsigned>(B + N) };
        for (int i = 0; i < B; ++i)
            chk_sum += row_parities[i];
        for (int j = 0; j < N; ++j)
            chk_sum += col_parities[j];
        return chk_sum;
    }

    void repair_byte_array(const ParityHdr& rcvd_hdr, const ParityHdr& t_hdr, unsigned char* t) {
        /* Repairs the received byte array, t, by comparing the rcvd_hdr with the one
           constructed in the receiving process, t_hdr, describing t.
        */

        // We first confirm that rcvd_hdr is identical (with high probability) to the
        // transmitted hdr, s_hdr(which we normally do not have), by checking the 
        // check_sum of rcvd_hdr. User normally does this prior to calling this fn.
        if (!rcvd_hdr.confirm_check_sum())
            throw std::runtime_error("BadCheckSum() in repair_byte_array");

        if (rcvd_hdr == t_hdr) // No repair needed. No need to call this fn in first place.
            return;

        // Following shouldn't fail as user should construct t_hdr from dimensions of rcvd_hdr:
        if (rcvd_hdr.B != t_hdr.B || rcvd_hdr.N != t_hdr.N)
            throw std::runtime_error("ParityHdr DimensionMismatch in repair_byte_array.");

        // Get here only if row and/or col_parities arrays differ.
        // Now find locations of intersections of these differences (one for now).
        int i{ -1 }, j{ -1 }; // i is bit row in [0, 8*B-1], j is bit col(==byte col) in [0, N-1].
        find_error_locations(rcvd_hdr, t_hdr, &i, &j);

        // Fix the bad bit: i is bit row, so locate byte first, then flip bit within that byte.
        t[j * rcvd_hdr.B + i / 8] ^= 0x80 >> i % 8;

        return;
    }

    void find_error_locations(const ParityHdr& rcvd_hdr, const ParityHdr& t_hdr, int* i, int* j) {
        /*
        On return, *i is the bit row in [0, 8*B-1], and *j is the col in [0, N-1], that contain the
        bad(flipped) bit (their intersection in the 8B x N matrix of bits is the bad bit.)
        throws PC_Exception if
         - more than 1 col or bit row had a bit flipped,
         - couldn't locate which col or row had the flipped bit(which happens when even number of
           bit flips happen in a bit row or col.).
         - more than 1 bad bit in a bad byte(which is a special case of first situation.)
        and what() can be called on the caught PC_Exception to tell what happened.
        When transmission errors are caught and the what() msg printed, they tend to be:
          "More than 1 col had a parity mismatch." -This is because col parities are checked before
          row parities so the many bad bits will throw that message first..
        */

        // First find the col, *j in [0, N-1], flipped bit is in:
        int n_errors = 0;  // used to count bad bits (aka: bit flips)
        for (int col = 0; col < rcvd_hdr.N; ++col) {
            if (rcvd_hdr.col_parities[col] != t_hdr.col_parities[col]) {
                n_errors += 1;
                *j = col;
            }
        }
        if (n_errors == 0)
            throw PC_Exception{ "In find_error_locations, Couldn't locate a col with a parity mismatch.\n" };
        if (n_errors > 1)
            throw PC_Exception{ "In find_error_locations, More than 1 col had a parity mismatch.\n" };

        // Find byte row in [0, B-1] with error:
        n_errors = 0;
        for (int row = 0; row < rcvd_hdr.B; ++row) {
            if (rcvd_hdr.row_parities[row] != t_hdr.row_parities[row]) {
                n_errors += 1;
                *i = row;  // save byte row with error.
            }
        }
        if (n_errors == 0)
            throw PC_Exception{ "In find_error_locations, Couldn't locate a row with a parity mismatch.\n" };
        if (n_errors > 1)
            throw PC_Exception{ "In find_error_locations, More than 1 row had a parity mismatch.\n" };

        // Find which bit, flipped_bit, within the byte row with parity mismatch, *i, is flipped:
        unsigned char flips = rcvd_hdr.row_parities[*i] ^ t_hdr.row_parities[*i]; // 1s at flipped bits.
        int flipped_bit{ -1 };
        int cnt{ 0 };    // number of bit flips found within the 1 bad byte, *i.
        for (int b = 0; b < 8; ++b) {
            if (0x80 & flips) {
                flipped_bit = b;
                ++cnt;
            }
            flips <<= 1;
        }
        if (cnt != 1)   // cnt > 0 because bad byte.
            throw PC_Exception{ "In find_error_locations, More than 1 bad bit found in the bad byte.\n" };
        // Convert to bit row in [0, 8*B-1] this flipped bit lies:
        *i = 8 * (*i) + flipped_bit;

        return;
    }

    bool operator== (const ParityHdr& lhs, const ParityHdr& rhs) {
        /* Not only do check_sums match(all you can do to compare received with transmitted),
           but also dimensions and row/col_parities match.  */
        if (lhs.check_sum != rhs.check_sum || lhs.B != rhs.B || lhs.N != rhs.N)
            return false;
        if (std::memcmp(lhs.row_parities, rhs.row_parities, lhs.B) != 0)
            return false;
        if (std::memcmp(lhs.col_parities, rhs.col_parities, lhs.N) != 0)
            return false;

        return true;
    }

    PC_Exception::PC_Exception(const char* es) : runtime_error{ es } {}
}
