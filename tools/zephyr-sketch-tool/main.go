// Copyright (c) Arduino s.r.l. and/or its affiliated companies
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bytes"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/binary"
	"encoding/pem"
	"flag"
	"fmt"
	"log"
	"os"
)

// MCUboot Header & Configuration Constants
const (
	ImageMagic       uint32 = 0x96f3b83d
	TlvInfoMagic     uint16 = 0x6907
	TlvKeyHash       uint8  = 0x01
	TlvSha256        uint8  = 0x10
	TlvRsa2048       uint8  = 0x20
	HeaderSize       uint16 = 0x400
	SlotSize         int    = 1540096
)

type ImageVersion struct {
	Major    uint8
	Minor    uint8
	Revision uint16
	BuildNum uint32
}

type ImageHeader struct {
	Magic     uint32
	LoadAddr  uint32
	HdrSize   uint16
	PTLVSize  uint16
	ImageSize uint32
	Flags     uint32
	Version   ImageVersion
	Pad1      uint32
}

func main() {
	var output = flag.String("output", "", "Output to a specific file (default: add -zsk.bin suffix)")
	var debug = flag.Bool("debug", false, "Enable debugging mode")
	var immediate = flag.Bool("immediate", false, "Start sketch immediately [UNO Q]")
	var wait_for_app = flag.Bool("wait_for_app", false, "Wait for the app to start [UNO Q]")
	var linked = flag.Bool("prelinked", false, "Provided file has already been linked to Zephyr")
	var force = flag.Bool("force", false, "Ignore safety checks and overwrite the header")
	var add_header = flag.Bool("add_header", false, "Add space for the header to the file")
	var signKey = flag.String("sign-key", "/home/daniele/arduino-zephyr/bootloader/mcuboot/root-rsa-2048.pem", "Path to the PEM file containing the RSA-2048 key to sign the sketch")

	// OTA mode: produce .ota update files from a mangled sketch + loader.
	var ota = flag.Bool("ota", false, "OTA mode: produce .ota update files")
	var otaLoader = flag.String("ota-loader", "", "[ota] loader binary path")
	var otaSketch = flag.String("ota-sketch", "", "[ota] sketch binary path (the mangled -zsk.bin artifact)")
	var otaOffset = flag.String("ota-offset", "", "[ota] sketch offset in merged binary (hex)")
	var otaMagic = flag.String("ota-magic", "", "[ota] board magic number (hex, 32-bit)")

	flag.Parse()

	if *ota {
		// Assuming runOTA is implemented elsewhere in your original environment
		if err := runOTA(*otaLoader, *otaSketch, *otaOffset, *otaMagic, *otaSketch); err != nil {
			fmt.Printf("OTA error: %v\n", err)
			os.Exit(1)
		}
		return
	}

	if flag.NArg() != 1 {
		fmt.Printf("Usage: %s [flags] <filename>\n", os.Args[0])
		flag.PrintDefaults()
		return
	}
	filename := flag.Arg(0)

	// Read the file content
	content, err := os.ReadFile(filename)
	if err != nil {
		fmt.Printf("Error reading file: %v\n", err)
		return
	}

	var ELF_HEADER = []byte{0x7f, 0x45, 0x4c, 0x46}
	var elf_header_found = bytes.Compare(ELF_HEADER, content[0:4]) == 0
	if *add_header || (!*force && !elf_header_found) {
		fmt.Printf("File does not have an ELF header, adding empty space\n")

		var newContent = make([]byte, len(content)+16)
		copy(newContent[16:], content)
		content = newContent
	}

	// Create and fill custom header
	var header struct {
		ver   uint8  // @ 0x07
		len   uint32 // @ 0x08
		magic uint16 // @ 0x0c
		flags uint8  // @ 0x0e
	}

	header.ver = 1
	header.magic = 0x2341 // Arduino USB VID
	header.len = uint32(len(content))

	header.flags = 0
	if *debug {
		header.flags |= 0x01
	}
	if *linked {
		header.flags |= 0x02
	}
	if *immediate {
		header.flags |= 0x04
	}
	if *wait_for_app {
		header.flags |= 0x08
	}

	var hdrBuf bytes.Buffer
	err = binary.Write(&hdrBuf, binary.LittleEndian, header)
	if err != nil {
		fmt.Printf("Error encoding header: %v\n", err)
		return
	}

	// Bytes 7 to 15 are free to use in current ELF specification. We will
	// use them to store the debug and linked flags.
	if !*force {
		for i := 7; i < 16; i++ {
			if content[i] != 0 {
				fmt.Printf("Target ELF header area is not empty. Use --force to overwrite\n")
				return
			}
		}
	}

	// Change the header bytes in the content
	copy(content[7:16], hdrBuf.Bytes())

	// Create a new filename for the final artifact
	newFilename := *output
	if newFilename == "" {
		newFilename = filename + "-zsk.bin"
	}

	// --- MCUboot SIGNING LOGIC ---
	if *signKey != "" {
		fmt.Println("Proceeding to sign the sketch using MCUboot format...")

		// 1. Write the raw unsigned content to a .no_signed backup file
		unsignedFilename := newFilename + ".no_signed"
		err = os.WriteFile(unsignedFilename, content, 0644)
		if err != nil {
			fmt.Printf("Error writing unsigned backup file: %v\n", err)
			return
		}
		fmt.Printf("Unsigned backup saved as %s\n", unsignedFilename)

		// 2. Load Private Key
		keyFile, err := os.ReadFile(*signKey)
		if err != nil {
			log.Fatalf("Failed to read key: %v", err)
		}

		block, _ := pem.Decode(keyFile)
		if block == nil {
			log.Fatalf("Failed to parse PEM block")
		}

		privKey, err := x509.ParsePKCS1PrivateKey(block.Bytes)
		if err != nil {
			log.Fatalf("Failed to parse RSA key: %v", err)
		}

		// Allocate new image buffer: MCUboot Header (0x400) + sketch payload
		totalSize := uint32(HeaderSize) + uint32(len(content))
		image := make([]byte, totalSize)
		
		// Place sketch payload immediately after the header space
		copy(image[HeaderSize:], content)

		// Construct MCUboot Image Header
		mcuHeader := ImageHeader{
			Magic:     ImageMagic,
			LoadAddr:  0x0,
			HdrSize:   HeaderSize,
			PTLVSize:  0,
			ImageSize: uint32(len(content)),
			Flags:     0x0,
			Version: ImageVersion{
				Major:    1,
				Minor:    0,
				Revision: 0,
				BuildNum: 0,
			},
		}

		var headerBuf bytes.Buffer
		err = binary.Write(&headerBuf, binary.LittleEndian, mcuHeader)
		if err != nil {
			log.Fatalf("Failed to write MCUboot header: %v", err)
		}
		// Write header into the first 32 bytes of the buffer
		copy(image[0:32], headerBuf.Bytes())

		// Hash the full image (Header + Payload)
		imgHash := sha256.Sum256(image)

		// Hash the Public Key
		pubKeyBytes := x509.MarshalPKCS1PublicKey(&privKey.PublicKey)
		keyHash := sha256.Sum256(pubKeyBytes)

		// Generate RSA-PSS Signature
		signature, err := rsa.SignPSS(rand.Reader, privKey, crypto.SHA256, imgHash[:], &rsa.PSSOptions{
			SaltLength: rsa.PSSSaltLengthEqualsHash,
		})
		if err != nil {
			log.Fatalf("Failed to sign payload: %v", err)
		}

		// Assemble final output
		var finalImg bytes.Buffer
		finalImg.Write(image)
		appendTLVs(&finalImg, keyHash[:], imgHash[:], signature)

		totalImageSize := finalImg.Len()
		if totalImageSize > SlotSize {
			log.Fatalf("Error: Final image size (%d) exceeds the defined slot size (%d)!", totalImageSize, SlotSize)
		}

		// 3. Write the signed .bin artifact to the primary filename
		err = os.WriteFile(newFilename, finalImg.Bytes(), 0644)
		if err != nil {
			log.Fatalf("Failed to write signed output: %v", err)
		}

		fmt.Printf("Signed image successfully saved as %s\n", newFilename)
	} else {
		// If no signing key is provided, just behave as before
		err = os.WriteFile(newFilename, content, 0644)
		if err != nil {
			fmt.Printf("Error writing to file: %v\n", err)
			return
		}

		fmt.Printf("File %s saved as %s\n", filename, newFilename)
	}
}

// appendTLVs handles the exact struct packing required by MCUboot
func appendTLVs(img *bytes.Buffer, keyHash []byte, imgHash []byte, sig []byte) {
	binary.Write(img, binary.LittleEndian, TlvInfoMagic)
	binary.Write(img, binary.LittleEndian, uint16(336)) // Size of standard RSA-2048 TLV block

	// 1. Write SHA256 TLV (Type 0x10)
	binary.Write(img, binary.LittleEndian, TlvSha256)
	binary.Write(img, binary.LittleEndian, uint8(0))
	binary.Write(img, binary.LittleEndian, uint16(len(imgHash)))
	img.Write(imgHash)

	// 2. Write KeyHash TLV (Type 0x01)
	binary.Write(img, binary.LittleEndian, TlvKeyHash)
	binary.Write(img, binary.LittleEndian, uint8(0))
	binary.Write(img, binary.LittleEndian, uint16(len(keyHash)))
	img.Write(keyHash)

	// 3. Write RSA Signature TLV (Type 0x20)
	binary.Write(img, binary.LittleEndian, TlvRsa2048)
	binary.Write(img, binary.LittleEndian, uint8(0))
	binary.Write(img, binary.LittleEndian, uint16(len(sig)))
	img.Write(sig)
}
