#Check if the correct number of arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required: <writefile> <writestr>"
    exit 1
fi

# Run clean
make clean

# Make with default targets
make all

# Execute compiled writer and pass arguments
./writer $1 $2

# Success
echo "Successfully wrote to $writefile"
exit 0