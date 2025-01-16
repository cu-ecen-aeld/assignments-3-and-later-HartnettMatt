#Check if the correct number of arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required: <writefile> <writestr>"
    exit 1
fi

# Assign arguments to variables
writefile=$1
writestr=$2

# Extract the directory path from the writefile
writedir=$(dirname "$writefile")

# Create the directory path if it does not exist
if ! mkdir -p "$writedir"; then
    echo "Error: Failed to create directory $writedir"
    exit 1
fi

# Write the string to the file, overwriting it if it exists
if ! echo "$writestr" > "$writefile"; then
    echo "Error: Failed to write to file $writefile"
    exit 1
fi

# Success
echo "Successfully wrote to $writefile"
exit 0