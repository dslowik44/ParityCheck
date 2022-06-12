#ifndef PARITY_CHECKING_HDR
#define PARITY_CHECKING_HDR

/*
namespace ParityChecking contains class ParityHdr and some associated utility functions
for error correcting transmission of byte arrays.
The ParityHdr class contains the parity information to be transmitted along with a
byte array allowing to detect (and correct in low noise transmissions) transmission errors.
Conceptually, the ParityHdr views the byte array as stored in a BxN matrix
(B rows and N cols) of bytes, (and so the byte arrays' length must factor as B * N).
The parity of the sequence of 8*B bits in each of the N cols is stored in the
col_parities array (0s and 1s of length N bytes).
The parity of the sequence of N bytes in each of the B rows is saved in the row_parities array
(of length B bytes)
So each byte of the row_parities array encodes the parity of 1 char row or 8 bit rows
  -- each bit of a row_parities array element is significant (encodes parity of that bit row.)
  On the other hand, each byte of the col_parities array is 0 or 1 denoting the parity of the
  B characters in a col.

All this information, stored in or pointed to by the ParityHdr, can then be serialized into a
byte array for transmission, along with the corresponding initial byte array, whose parity
information it contains.
On reception, this ParityHdr can be de-serialized/reconstructed and compared with a new ParityHdr,
constructed from the received initial byte array, to detect errors.
A parity flip in one row must match up with a parity flip in some col (statements true in limit of low
probability of error on any one bit, probabilites can be worked out after several transmissions)
which allows correction of the bit at their intersection in the original byte array.
Probability of error in ParityHdr is much smaller than for error in original byte array for long byte arrays.
Parity errors in 2 rows and in 2 cols result in 4 ambiguous characters in the original byte array 
corresponding to bit flips at the 4 possible intersections; these could be delt with in future versions.
(e.g., if english text is being transmitted, a dictionary of words can be used to aid reconstruction.)
In this version, a maximum of 1 parity error per row and per col is allowed.
If the number of rows with parity errors is not equal the number cols with parity errors, or this number 
is larger than 1, the byte array is re-transmitted until it is.
etc.
*/
#include <stdexcept>

namespace ParityChecking {

  class ParityHdr {
    public:
    ParityHdr(); 
    // Use this to construct ParityHdr corresponding to some byte array before transmitting:
    ParityHdr(unsigned short B, unsigned short N, const unsigned char* byte_array);
    ~ParityHdr() { delete[] row_parities; delete[] col_parities; }
    ParityHdr(const ParityHdr& ) = delete;
    ParityHdr& operator= (const ParityHdr& ) = delete;

    // These 2 used by receiver to match transmitted ParityHdr dimensions:
    unsigned int getB() { return B; }
    unsigned int getN() { return N; }

    const unsigned char* serialize() const; // User calls this prior to ParityHdr transmission.
    bool load_from_serialized(const unsigned char*);  // Load empty ParityHdr from received bytes.
    bool confirm_check_sum() const;   // User can confirm received ParityHdr is good to extent possible.

    // Use recieved ParityHdr, rcvd_hdr,(after confirm_check_sum) and ParityHdr, t_hdr, 
    // of received byte array, t, to fix 1 bit flip in t when rcvd_hdr != t_hdr: 
    friend bool operator== (const ParityHdr&, const ParityHdr&);
    friend void repair_byte_array(const ParityHdr& rcvd_hdr, const ParityHdr& t_hdr,
      unsigned char* t);
    friend void find_error_locations(const ParityHdr&, const ParityHdr&, int*, int*);

    private:
    void calculate_parities(const unsigned char*);  // called by ctor to fill row/col_parities.
    inline unsigned char byte_parity(unsigned char) const;
    unsigned int calc_check_sum() const;

    unsigned int check_sum;  // == B + N + sum(row_parities) + sum(col_parities)
    unsigned short B;          // Number of bytes per column.
    unsigned short N;          // Number of columns.
    unsigned char* row_parities;   // in [0, 255] tracks parity of each bit row within a byte row. 
    unsigned char* col_parities;   // 0 or 1.
  };

  unsigned char ParityHdr::byte_parity(unsigned char c) const {
    // returns 0 or 1 parity of byte c.
    unsigned char p{ 0 };
    for (int i = 0; i < 8; ++i) {
      p ^= c & 1;
      c >>= 1;
    }
    return p;
  }

  inline bool operator!= (const ParityHdr& lhs, const ParityHdr& rhs) { return !(lhs == rhs); }

  // ParityChecking exceptions ctor takes a c_str accesible via what() in catch.
  class PC_Exception : public std::runtime_error {
    public:
    PC_Exception(const char*);
  };


  void repair_byte_array(const ParityHdr& rcvd_hdr, const ParityHdr& t_hdr,
    unsigned char* t);
}

#endif