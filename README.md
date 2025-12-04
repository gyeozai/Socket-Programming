# CN2024 project phase 2

## Requirements
- Multithread server (15pts)
- Sending Chat Messages (20pts)
- Message Encryption with OpenSSL (10pts)
- Transfer files (10pts)
- <span style="color:red;">Audio/Video Streaming (10pts)</span>
- [Bonus] <span style="color:red;">Microphone and Webcam Integration (10pts)</span>
- [Bonus] GUI Interface (5~15pts)

## Compilation instructions

### Environment
- Ubuntu 22.04
- MySQL 8.0.4
- OpenSSL 3.0.2
- GTK+ 3.24.33

### Prerequisites
- Install MySQL server - pls refers to [here](#other-information).
- Replace `PASSWORD` in `"path to project directory"/b11902091/code/include/server.h` to your own MySQL password.
- Run the following command to install `OpenSSL` and `GTK+ 3.0`
   ```bash
   sudo apt update
   sudo apt install openssl libssl-dev libgtk-3-dev
   ```
### Instructions to Execute
1. Navigate to the Project Directory
   ```bash
   cd "path to project directory"/b11902091/code
   ```
   - Replace `"path to project directory"` with the actual path to your project folder.
   
2. Clean Up Unnecessary Files
   ```bash
   make clean
   ```
   - It helps remove the executables `server` and `client` generated before, as well as the directories `build`, `certs`, `files` and `videos`.
3. Set Up and Build
   ```bash
   make all
   ```
   - It helps generate the executables `server` and `client`, create OpenSSL keys and certificates, and then sets up the directories such as `build`, `certs`, `files` and `videos`.
4. Start the Server
   ```bash
   ./server {server port}
   ```
5. Start the Client
   ```bash
   ./client {server ip} {server port}
   ```

## Usage Guide
### Check Database Status
1. Open your terminal and log in to the MySQL server
   ```shell
   mysql -u root -p
   ```
   - If you encounter an access denied error, pls refers to [here](#other-information).
2. Useful Commands for Managing the Database
   ```sql
   show databases;          -- list all available databases
   use database chatIN;     -- select specific database
   show tables;             -- list all tables of selected database
   select * from Users;     -- vieq all data from specific table
   drop database chatIN;    -- delete specific database
   ```

### Commands for the server
1. `Ctrl + C` - to shut down the server gracefully

### Command for the client
1. `register`   - register a new account (duplicate registers with the **same username** are not allowed)
2. `login`      - log in to chatIN server (duplicate logins for the **same account** are not allowed)
3. `exit`       - exit the client application
4. `chat`       - invite other online but idle user to chat together
4. `file`       - for file transfer feature, such as `upload` and `download`
5. `logout`     - log out from chatIN server
   
## Other Information
### External Resources
1. [Project Specification](https://docs.google.com/presentation/d/1S_yBa2GHjt8t82sMy_8JexNUmDyMOfw3lp07j4psx08/edit#slide=id.p)
2. [Demo Video for phase 2]()
3. [Installing MySQL Server](https://www.geeksforgeeks.org/how-to-install-mysql-on-ubuntu/)
4. [Solving Access Denied Error](https://phoenixnap.com/kb/access-denied-for-user-root-localhost)