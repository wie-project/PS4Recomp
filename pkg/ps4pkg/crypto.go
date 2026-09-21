package ps4pkg

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"math/big"
)

const (
	pfsFlagNestedImage = 0x8000000000000000
	pfsFlagNewCrypt    = 0x2000000000000000
)

func sha256Sum(data []byte) []byte {
	sum := sha256.Sum256(data)
	out := make([]byte, sha256.Size)
	copy(out, sum[:])
	return out
}

func hmacSHA256(key, data []byte) []byte {
	mac := hmac.New(sha256.New, key)
	_, _ = mac.Write(data)
	return mac.Sum(nil)
}

func xorBytes(a, b []byte) []byte {
	n := len(a)
	if len(b) < n {
		n = len(b)
	}
	out := make([]byte, n)
	for i := 0; i < n; i++ {
		out[i] = a[i] ^ b[i]
	}
	return out
}

func aesCBCDecrypt(ciphertext, key, iv []byte) ([]byte, error) {
	if len(key) != 16 || len(iv) != 16 {
		return nil, errors.New("AES-128-CBC requires 16-byte key and IV")
	}
	if len(ciphertext)%aes.BlockSize != 0 {
		return nil, fmt.Errorf("ciphertext length %d is not a multiple of AES block size", len(ciphertext))
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	out := make([]byte, len(ciphertext))
	cipher.NewCBCDecrypter(block, iv).CryptBlocks(out, ciphertext)
	return out, nil
}

func rsaPKCS1Decrypt(priv *rsa.PrivateKey, ciphertext []byte) ([]byte, error) {
	pt, err := rsa.DecryptPKCS1v15(nil, priv, ciphertext)
	if err == nil {
		return pt, nil
	}
	// Some packages store a raw RSA block (no PKCS#1 type-2 padding).
	c := new(big.Int).SetBytes(ciphertext)
	m := new(big.Int).Exp(c, priv.D, priv.N)
	raw := m.FillBytes(make([]byte, 256))
	return raw, nil
}

func takeKey32(b []byte) []byte {
	if len(b) >= 32 {
		return append([]byte(nil), b[len(b)-32:]...)
	}
	out := make([]byte, 32)
	copy(out[32-len(b):], b)
	return out
}

// ComputeKeys derives a 32-byte PKG key from content ID, passcode and index.
// Index 1 is EKPFS.
func ComputeKeys(contentID, passcode string, index uint32) ([]byte, error) {
	if len(contentID) != 36 {
		return nil, fmt.Errorf("content ID must be 36 characters, got %d", len(contentID))
	}
	if len(passcode) != 32 {
		return nil, fmt.Errorf("passcode must be 32 characters, got %d", len(passcode))
	}

	idx := make([]byte, 4)
	binary.BigEndian.PutUint32(idx, index)

	cid := make([]byte, 48)
	copy(cid, []byte(contentID))

	data := make([]byte, 96)
	copy(data[0:32], sha256Sum(idx))
	copy(data[32:64], sha256Sum(cid))
	copy(data[64:96], []byte(passcode))
	return sha256Sum(data), nil
}

// PfsGenCryptoKey is HMAC-SHA256(ekpfs, index_le || seed).
func PfsGenCryptoKey(ekpfs, seed []byte, index uint32) []byte {
	d := make([]byte, 4+len(seed))
	binary.LittleEndian.PutUint32(d[:4], index)
	copy(d[4:], seed)
	return hmacSHA256(ekpfs, d)
}

// PfsGenEncKey returns (tweakKey, dataKey) for AES-XTS of a PFS image.
func PfsGenEncKey(ekpfs, seed []byte, newCrypt bool) (tweakKey, dataKey []byte) {
	hmacKey := ekpfs
	if newCrypt {
		hmacKey = hmacSHA256(ekpfs, seed)
	}
	encKey := PfsGenCryptoKey(hmacKey, seed, 1)
	tweakKey = make([]byte, 16)
	dataKey = make([]byte, 16)
	copy(tweakKey, encKey[0:16])
	copy(dataKey, encKey[16:32])
	return tweakKey, dataKey
}
