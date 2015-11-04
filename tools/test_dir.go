package main

/*
This is meant to check that making changes didn't break anything.

You can run it against it directory with .chm files, create a reference
file that records the output for files identified by their sha1.

This reference can be saved somewhere e.g. in dropbox. Update referenceFiles.

Then we can re-run with -check-ref option against directory, which will run
against .chm files in directory and check that output is the same as recorded
in reference file.
*/

import (
	"bytes"
	"compress/bzip2"
	"crypto/sha1"
	"errors"
	"flag"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/http/cookiejar"
	"net/http/httputil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/net/publicsuffix"
)

var (
	testExe        = "obj/clang/rel/test"
	referenceFiles = []string{
		"https://www.dropbox.com/s/4t0yhhgwsbs3a35/reference1.txt.bz2",
		"reference1.txt.bz2",
		"a3839fadec11e7852e538f37f91e4e9d7c5811b4",
		"https://www.dropbox.com/s/t8dldr121rtdmnn/reference2.txt.bz2",
		"reference2.txt.bz2",
		"83528aa4c1441cb3c907cd158add70195a3dd216",
	}
	seenFiles        map[string]bool
	referenceResults map[string][]string
	nFile            int
	timeStart        time.Time
	flgCheckRef      bool
	priorityFiles    = []string{
		"/Volumes/Store/books/_chm/Automating UNIX And Linux Administration (2003).chm",
		"/Volumes/Store/books/_chm/Que.Mobile.Guide.to.BlackBerry.May.2005.eBook-LiB.ch",
		"/Volumes/Store/books/_chm/Linux Unwired (2004).chm",
		"/Volumes/Store/tmp/HD1TB/books/chm/XML A Manager's Guide 2nd Edition.chm",
	}
	// TODO: use those files to test we don't crash anymore
	pastCrashers = []string{
		"https://www.dropbox.com/s/ubch71i1hmnlr0t/crash1.chm?dl=0",
		"651379a64c778774354456ff12a3bdd6c0e2536e",
	}
)

// White-listed changes:

/*
Probably because what used to be an error in decompression, no longer is an
error. Seen first in https://github.com/sumatrapdfreader/chmlib/commit/1775b5298fcd3feb3df7a4140a6eb37b7c68f099
so a change must have happened before this.

different results for '7575a94fb9bebd9eab9f1c038f22e12917d94c84' on line 351, file '/Volumes/Store/books/_chm/Que.Mobile.Guide.to.BlackBerry.May.2005.eBook-LiB.chm'
expected: '1,1874347,1093,file,0000000000000000000000000000000000000000,/0789733439/images/0789733439/graphics/browser.gif;400479'
got     : '1,1874347,1093,file,464320F04EB262006A3B1EB74D1A2F897ECA9986,/0789733439/images/0789733439/graphics/browser.gif;400479'

*/
var (
	whiteListed = []string{
		"7575a94fb9bebd9eab9f1c038f22e12917d94c84",
	}
)

func init() {
	seenFiles = make(map[string]bool)
	referenceResults = make(map[string][]string)
}

func isWhiteListed(sha1 string) bool {
	for _, s := range whiteListed {
		if sha1 == s {
			return true
		}
	}
	return false
}

func seenSha1(sha1Hex string) bool {
	seen := seenFiles[sha1Hex]
	if seen {
		return true
	}
	seenFiles[sha1Hex] = true
	return false
}

func fileSha1Hex(path string) (string, error) {
	d, err := ioutil.ReadFile(path)
	if err != nil {
		return "", err
	}
	sha1 := sha1.Sum(d)
	return fmt.Sprintf("%x", sha1[:]), nil
}

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

func toTrimmedLines(d []byte) []string {
	lines := strings.Split(string(d), "\n")
	for i, l := range lines {
		l = strings.TrimSpace(l)
		lines[i] = l
	}
	return lines
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

func lineDiffIndex(l1, l2 []string) int {
	n := len(l1)
	for i := 0; i < n; i++ {
		if l1[i] != l2[i] {
			return i
		}
	}
	return -1
}

func outputToLines(d []byte) []string {
	lines := toTrimmedLines(d)
	for {
		n := len(lines)
		if n == 0 {
			return lines
		}
		s := lines[n-1]
		if len(s) > 0 {
			return lines
		}
		lines = lines[:n-2]
	}
}

func checkRefFile(path string) error {
	sha1Hex, err := fileSha1Hex(path)
	if err != nil {
		return err
	}
	if seenSha1(sha1Hex) {
		return nil
	}
	nFile++
	expectedLines, ok := referenceResults[sha1Hex]
	if !ok {
		fmt.Printf("don't have reference result for '%s'\n", sha1Hex)
		return fmt.Errorf("don't have reference results for '%s'", path)
	}
	stdout, stderr, err := runTest(path)
	if err != nil {
		return err
	}

	var buf bytes.Buffer
	if len(stdout) != 0 {
		fmt.Fprintf(&buf, "%s\n", stdout)
	}
	if len(stderr) != 0 {
		fmt.Fprintf(&buf, "stderr:\n'%s'\n", stderr)
	}
	d := buf.Bytes()
	lines := outputToLines(d)

	if len(lines) != len(expectedLines) {
		fmt.Printf("different results for '%s', '%s'\n", sha1Hex, path)
		fmt.Printf("len(lines) = %d, len(expectedLines) = %d\n", len(lines), len(expectedLines))

		if isWhiteListed(sha1Hex) {
			fmt.Printf("is whitelisted!\n")
			return nil
		}

		n := len(expectedLines)
		if len(lines) > n {
			n = len(lines)
		}
		for i := 0; i < n; i++ {
			s1 := ""
			s2 := ""
			if i < len(expectedLines) {
				s1 = expectedLines[i]
			}
			if i < len(lines) {
				s2 = lines[i]
			}
			if s1 != s2 {
				fmt.Printf("got: '%s'\nexp: '%s'\n\n", s2, s1)
			}
		}
		return fmt.Errorf("mismatch for file '%s' of sha1 '%s'", path, sha1Hex)
	}
	idx := lineDiffIndex(lines, expectedLines)
	if idx != -1 {
		if isWhiteListed(sha1Hex) {
			fmt.Printf("%s: mismatch but whitelisted!\n", sha1Hex)
			return nil
		}
		fmt.Printf("different results for '%s' on line %d, file '%s'\n", sha1Hex, idx, path)
		fmt.Printf("expected: '%s'\n", expectedLines[idx])
		fmt.Printf("got     : '%s'\n", lines[idx])
		return fmt.Errorf("mismatch for file '%s' of sha1 '%s'", path, sha1Hex)
	}
	fmt.Printf("%s: ok!, %d, %s\n", sha1Hex, nFile, time.Since(timeStart))
	return nil
}

func testFile(path string) error {
	sha1Hex, err := fileSha1Hex(path)
	if err != nil {
		return err
	}
	if seenSha1(sha1Hex) {
		return nil
	}

	fmt.Printf("File: %s\n", sha1Hex)
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
}

func testPriorityFiles(dir string) error {
	for _, path := range priorityFiles {
		err := checkRefFile(path)
		if err != nil && !ignoreErrorForPriorityFiles(err) {
			fmt.Printf("err: '%s'\n", err)
			return err
		}
	}
	return nil
}

func ignoreErrorForPriorityFiles(err error) bool {
	s := err.Error()
	if strings.Contains(s, "don't have reference results") {
		return true
	}
	if strings.Contains(s, "no such file") {
		return true
	}
	return false
}

func testDir(dir string) {
	err := testPriorityFiles(dir)
	if err != nil {
		return
	}
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
		if flgCheckRef {
			return checkRefFile(path)
		}
		return testFile(path)
	})
}

func parseFlags() {
	flag.BoolVar(&flgCheckRef, "check-ref", false, "run in reference checking mode")
	flag.Parse()
}

func isStartLine(s string) bool {
	// we used to have a typo in older code
	s = strings.ToLower(s)
	return strings.HasPrefix(s, "staring in") || strings.HasPrefix(s, "starting in")
}

func parseFileLine(s string) string {
	parts := strings.SplitN(s, ": ", 2)
	if len(parts) != 2 {
		return ""
	}
	if parts[0] != "File" {
		return ""
	}
	sha1 := parts[1]
	if len(sha1) != 40 {
		return ""
	}
	return sha1
}

func rememberReferenceFile(sha1 string, out []string) {
	// TODO: if we already have it, check that out is the same
	referenceResults[sha1] = out
}

func parseReferenceFile(d []byte) error {
	lines := toTrimmedLines(d)
	if isStartLine(lines[0]) {
		lines = lines[1:]
	}
	for {
		//fmt.Printf("line: '%s'\n", lines[0])
		sha1 := parseFileLine(lines[0])
		if len(sha1) != 40 {
			return fmt.Errorf("parseReferenceFile: unexpected File: line '%s'", lines[0])
		}
		lines = lines[1:]
		var outLines []string
		for i := 0; i < len(lines) && len(lines[i]) > 0; i++ {
			outLines = append(outLines, lines[i])
		}
		n := len(outLines)
		lines = lines[n:]

		if len(lines) > 0 {
			if len(lines[0]) != 0 {
				return fmt.Errorf("expected empty line, got '%s'", lines[0])
			}
			lines = lines[1:]
		}
		for len(lines) > 0 && len(lines[0]) == 0 {
			lines = lines[1:]
		}
		rememberReferenceFile(sha1, outLines)
		if len(lines) == 0 {
			return nil
		}
	}
}

// we assume that they are .bz2 files
func loadAndParseReferenceFile(path string) error {
	//fmt.Printf("loading '%s'\n", path)
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	r := bzip2.NewReader(f)
	d, err := ioutil.ReadAll(r)
	if err != nil {
		return err
	}
	return parseReferenceFile(d)
}

const (
	debugHTTP = false
)

func dumpReq(req *http.Request) {
	if debugHTTP {
		d, _ := httputil.DumpRequest(req, false)
		fmt.Printf("%s\n", string(d))
	}
}

func dumpResp(rsp *http.Response) {
	if debugHTTP {
		d, _ := httputil.DumpResponse(rsp, false)
		fmt.Printf("%s\n", string(d))
	}
}

func httpDl(uri, fileName string) error {
	fmt.Printf("httpDl: %s\n", uri)
	options := cookiejar.Options{
		PublicSuffixList: publicsuffix.List,
	}
	jar, err := cookiejar.New(&options)
	if err != nil {
		return err
	}
	client := http.Client{Jar: jar}
	client.CheckRedirect = func(req *http.Request, via []*http.Request) error {
		dumpReq(req)
		if len(via) >= 10 {
			return fmt.Errorf("too many redirects")
		}
		if len(via) == 0 {
			return nil
		}
		for attr, val := range via[0].Header {
			if _, ok := req.Header[attr]; !ok {
				req.Header[attr] = val
			}
		}
		return nil
	}

	req, err := http.NewRequest("GET", uri, nil)
	// Note: this is crucial. Dropbox will return some html if User-Agent is not defined
	req.Header.Add("User-Agent", "curl/7.43.0")
	dumpReq(req)
	if err != nil {
		return err
	}
	resp, err := client.Do(req)
	dumpResp(resp)
	if err != nil {
		return err
	}
	d, err := ioutil.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		return err
	}
	if resp.StatusCode != 200 {
		return fmt.Errorf("httpDl() failed because StatusCode = %d", resp.StatusCode)
	}
	return ioutil.WriteFile(fileName, d, 0644)
}

func downloadAndParseReferenceFile(i int) error {
	uri := referenceFiles[i*3]
	fileName := referenceFiles[i*3+1]
	expectedSha1Hex := referenceFiles[i*3+2]
	if !fileExists(fileName) {
		fmt.Printf("file '%s' doesn't exist, downloading...\n", fileName)
		err := httpDl(uri, fileName)
		if err != nil {
			return err
		}
		fmt.Printf("downloaded ok!\n")
	}
	sha1Hex, err := fileSha1Hex(fileName)
	if err != nil {
		return err
	}
	if sha1Hex != expectedSha1Hex {
		return fmt.Errorf("Invalid sha1 for '%s'. Is %s, should be %s", fileName, sha1Hex, expectedSha1Hex)
	}
	return loadAndParseReferenceFile(fileName)
}

func downloadReferenceFiles() error {
	n := len(referenceFiles) / 3
	//fmt.Printf("downloadReferenceFiles(): n=%d\n", n)
	for i := 0; i < n; i++ {
		err := downloadAndParseReferenceFile(i)
		if err != nil {
			return err
		}
	}
	return nil
}

func main() {
	parseFlags()

	if len(flag.Args()) != 1 {
		fmt.Printf("usage: test_dir [-check-ref] <dir>\n")
		os.Exit(1)
	}
	if !fileExists(testExe) {
		fmt.Printf("'%s' doesn't exist\n", testExe)
		os.Exit(1)
	}
	if flgCheckRef {
		err := downloadReferenceFiles()
		if err != nil {
			fmt.Printf("downloadReferenceFiles() failed with '%s'\n", err)
			os.Exit(1)
		}

		if false {
			for sha1, outLines := range referenceResults {
				fmt.Printf("%s: %d\n", sha1, len(outLines))
			}
		}
		fmt.Printf("loaded %d reference results\n", len(referenceResults))
	}
	dir := flag.Args()[0]
	fmt.Printf("starting in '%s'\n", dir)
	timeStart = time.Now()
	testDir(dir)
}
