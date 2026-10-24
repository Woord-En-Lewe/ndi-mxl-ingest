// SPDX-License-Identifier: MIT
// Run from ci/: go run . (requires a working Dagger container engine).
package main

import (
	"context"
	"dagger.io/dagger"
	"fmt"
	"os"
	"time"
)

func run(ctx context.Context) error {
	client, err := dagger.Connect(ctx, dagger.WithLogOutput(os.Stderr))
	if err != nil {
		return err
	}
	defer client.Close()
	source := client.Host().Directory("..", dagger.HostDirectoryOpts{Exclude: []string{".git", "build*", "stage"}})
	result := client.Container().From("debian:bookworm-slim").
		WithExec([]string{"apt-get", "update"}).
		WithExec([]string{"apt-get", "install", "-y", "--no-install-recommends", "clang", "cmake", "ninja-build", "ca-certificates"}).
		WithDirectory("/src", source).WithWorkdir("/src").
		WithExec([]string{"cmake", "-S", ".", "-B", "/build", "-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_BUILD_TYPE=Debug", "-DINGEST_SANITIZE=ON"}).
		WithExec([]string{"cmake", "--build", "/build"}).
		WithExec([]string{"ctest", "--test-dir", "/build", "--output-on-failure"})
	out, err := result.Stdout(ctx)
	if err != nil {
		return err
	}
	fmt.Print(out)
	return nil
}
func main() {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Minute)
	defer cancel()
	if err := run(ctx); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
