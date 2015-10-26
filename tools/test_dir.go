package main

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

var (
	testExe = "obj/clang/rel/test"
)

func fileExists(path string) bool {
	fi, err := os.Stat(path)
	if err != nil {
		return false
	}
	return fi.Mode().IsRegular()
}

func isChm(path string) bool {
	return strings.ToLower(filepath.Ext(path)) == ".chm"
}

func isErrPermDenied(err error) bool {
	return strings.Contains(err.Error(), "permission denied")
}

func runTest(path string) ([]byte, []byte, error) {
	cmd := exec.Command(testExe, path)

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	err := cmd.Start()
	if err != nil {
		return nil, nil, err
	}
	err = cmd.Wait()
	return stdout.Bytes(), stderr.Bytes(), err
}

func testDir(dir string) {
	filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			if !isErrPermDenied(err) {
				fmt.Printf("error on path: '%s', error: '%s'\n", path, err)
			}
			return nil
		}
		if info.IsDir() || !info.Mode().IsRegular() || !isChm(path) {
			return nil
		}
		fmt.Printf("%s\n", path)
		stdout, stderr, err := runTest(path)
		if err != nil {
			fmt.Printf("failed with '%s' on '%s'\n", err, path)
			if len(stdout) != 0 {
				fmt.Printf("stdout:\n'%s'\n", stdout)
			}
			if len(stderr) != 0 {
				fmt.Printf("stderr:\n'%s'\n", stderr)
			}
			return errors.New("stoped because test failed on file")
		}
		if len(stdout) != 0 {
			fmt.Printf("%s\n", stdout)
		}
		if len(stderr) != 0 {
			fmt.Printf("stderr:\n'%s'\n", stderr)
		}
		return nil
	})
}

func main() {
	if len(os.Args) != 2 {
		fmt.Printf("usage: test_dir <dir>\n")
		os.Exit(1)
	}
	if !fileExists(testExe) {
		fmt.Printf("'%s' doesn't exist\n", testExe)
		os.Exit(1)
	}
	dir := os.Args[1]
	fmt.Printf("staring in '%s'\n", dir)
	testDir(dir)
}
