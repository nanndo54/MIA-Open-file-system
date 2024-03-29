#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <boost/algorithm/string.hpp>

#include "usermanager.h"
#include "storagemanager.h"
#include "../Util/util.h"
#include "../Structs/diskstructs.h"
#include "../Structs/mountstructs.h"
#include "../Structs/partitionstructs.h"
#include "../Util/util.h"

using namespace std;

bool logged;
string user_logged;
int user_num;
int group_num;
const char *disk_path;
string partition_name;

int login(string usuario, string password, string id)
{
    if (logged)
        throw Exception("session already active; logout before logging in");

    if (id.length() < 4 || id.length() > 5)
        throw Exception("non-valid id");
    if (id.substr(0, 2) != "98")
        throw Exception("non-valid id (ids start with '98')");

    const char *path;
    string name;
    vector<const char *> data;

    if (id.length() == 4)
        data = getPartitionMountedByID(id.substr(3, 4)[0], stoi(id.substr(2, 3)));
    else
        data = getPartitionMountedByID(id.substr(4, 5)[0], stoi(id.substr(2, 4)));

    if (data.empty())
        throw Exception("id does not exist");

    path = data[1];
    name = data[0];

    FILE *file = fopen(path, "rb");
    fseek(file, 0, SEEK_SET);

    MBR mbr;
    fread(&mbr, sizeof(MBR), 1, file);

    Partition *partition = getPartition(file, &mbr, name);

    if (partition->status != '1')
        throw Exception("partition is not formatted");

    superblock super;
    fseek(file, partition->start, SEEK_SET);
    fread(&super, sizeof(superblock), 1, file);

    inode inodo;
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fread(&inodo, sizeof(inode), 1, file);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char date[17];
    strftime(date, 17, "%d/%m/%Y %H:%M", tm);
    strncpy(inodo.atime, date, 16);

    string content = read_inode_content(file, super, inodo);

    stringstream ss(content);
    string line, group;
    int user_num_temp, group_num_temp;
    vector<vector<string>> groups;

    bool found = false;

    while (getline(ss, line, '\n'))
    {
        stringstream us(line);
        string substr;
        vector<string> words;

        while (getline(us, substr, ','))
        {
            words.push_back(substr);
        }

        if (words.at(1) == "G")
            groups.push_back(words);

        if (words.at(1) == "U")
            if (words.at(3) == usuario)
            {
                found = true;

                if (words.at(0) == "0")
                    throw Exception("non-existent user");

                user_num_temp = stoi(words.at(0));
                group = words.at(2);

                if (words.at(4) != password)
                {
                    fclose(file);
                    throw Exception("wrong password");
                }
            }
    }

    if (!found)
        throw Exception("non-existent user");

    for (vector<string> grp : groups)
        if (grp.at(2) == group)
        {
            if (grp.at(0) == "0")
                throw Exception("non-existent user");

            group_num_temp = stoi(grp.at(0));
        }

    logged = true;
    user_logged = usuario;
    user_num = user_num_temp;
    group_num = group_num_temp;
    disk_path = path;
    partition_name = name;

    fclose(file);

    return 0;
}

int logout()
{
    if (!logged)
        throw Exception("no session active");

    logged = false;

    return 0;
}

int mkgrp(string name)
{
    if (!logged)
        throw Exception("no session active");

    if (user_logged != "root")
        throw Exception("user has no permissions");

    if (name.length() > 10)
    {
        name = name.substr(0, 10);
        cout << "name truncated to '" << name << "'" << endl;
    }

    FILE *file = fopen(disk_path, "rb");
    fseek(file, 0, SEEK_SET);

    MBR mbr;
    fread(&mbr, sizeof(MBR), 1, file);

    Partition *partition = getPartition(file, &mbr, partition_name);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char date[17];
    strftime(date, 17, "%d/%m/%Y %H:%M", tm);

    superblock super;
    fseek(file, partition->start, SEEK_SET);
    fread(&super, sizeof(superblock), 1, file);

    inode inodo;
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fread(&inodo, sizeof(inode), 1, file);

    string content = read_inode_content(file, super, inodo);

    stringstream ss(content);
    string line;
    int new_index = 1;

    try
    {
        while (getline(ss, line, '\n'))
        {
            stringstream us(line);
            string substr;
            vector<string> words;

            while (getline(us, substr, ','))
            {
                words.push_back(substr);
            }

            if (words.at(1) == "G")
            {
                new_index += 1;
                if (words.at(2) == name)
                    throw Exception("already existent group");
            }
        }

        content += std::to_string(new_index) + ",G," + name + "\n";

        file = fopen(disk_path, "r+b");
        write_inode_content(file, &super, &inodo, content);
    }
    catch (const Exception &e)
    {
        strncpy(inodo.atime, date, 16);
        fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
        fwrite(&inodo, sizeof(inode), 1, file);

        strncpy(super.umtime, date, 16);
        fseek(file, partition->start, SEEK_SET);
        fwrite(&super, sizeof(superblock), 1, file);

        throw Exception(e.what());
    }

    strncpy(inodo.mtime, date, 16);
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fwrite(&inodo, sizeof(inode), 1, file);

    strncpy(super.mtime, date, 16);
    fseek(file, partition->start, SEEK_SET);
    fwrite(&super, sizeof(superblock), 1, file);

    fclose(file);

    return 0;
}

int rmgrp(string name)
{
    if (!logged)
        throw Exception("no session active");

    if (user_logged != "root")
        throw Exception("user has no permissions");

    FILE *file = fopen(disk_path, "rb");
    fseek(file, 0, SEEK_SET);

    MBR mbr;
    fread(&mbr, sizeof(MBR), 1, file);

    Partition *partition = getPartition(file, &mbr, partition_name);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char date[17];
    strftime(date, 17, "%d/%m/%Y %H:%M", tm);

    superblock super;
    fseek(file, partition->start, SEEK_SET);
    fread(&super, sizeof(superblock), 1, file);

    inode inodo;
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fread(&inodo, sizeof(inode), 1, file);

    string content = read_inode_content(file, super, inodo);

    stringstream ss(content);
    string line;
    string new_content = "";

    try
    {
        bool found = false;
        while (getline(ss, line, '\n'))
        {
            stringstream us(line);
            string substr;
            vector<string> words;

            while (getline(us, substr, ','))
            {
                words.push_back(substr);
            }

            if (words.at(1) == "G")
            {
                if (words.at(0) != "0" && words.at(2) == name)
                {
                    found = true;
                    new_content += "0,G," + words.at(2) + "\n";
                    continue;
                }
            }

            new_content += line + "\n";
        }

        if (!found)
            throw Exception("non-existent group");

        file = fopen(disk_path, "r+b");
        write_inode_content(file, &super, &inodo, content);
    }
    catch (const Exception &e)
    {
        strncpy(inodo.atime, date, 16);
        fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
        fwrite(&inodo, sizeof(inode), 1, file);

        strncpy(super.umtime, date, 16);
        fseek(file, partition->start, SEEK_SET);
        fwrite(&super, sizeof(superblock), 1, file);

        throw Exception(e.what());
    }

    strncpy(inodo.mtime, date, 16);
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fwrite(&inodo, sizeof(inode), 1, file);

    strncpy(super.mtime, date, 16);
    fseek(file, partition->start, SEEK_SET);
    fwrite(&super, sizeof(superblock), 1, file);

    fclose(file);

    return 0;
}

int mkusr(string usr, string pwd, string grp)
{
    if (!logged)
        throw Exception("no sesion active");

    if (user_logged != "root")
        throw Exception("user has no permissions");

    if (usr.length() > 10)
    {
        usr = usr.substr(0, 10);
        cout << "usr truncated to '" << usr << "'" << endl;
    }
    if (pwd.length() > 10)
    {
        pwd = pwd.substr(0, 10);
        cout << "pwd truncated to '" << pwd << "'" << endl;
    }
    if (grp.length() > 10)
    {
        grp = grp.substr(0, 10);
        cout << "grp truncated to '" << grp << "'" << endl;
    }

    FILE *file = fopen(disk_path, "rb");
    fseek(file, 0, SEEK_SET);

    MBR mbr;
    fread(&mbr, sizeof(MBR), 1, file);

    Partition *partition = getPartition(file, &mbr, partition_name);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char date[17];
    strftime(date, 17, "%d/%m/%Y %H:%M", tm);

    superblock super;
    fseek(file, partition->start, SEEK_SET);
    fread(&super, sizeof(superblock), 1, file);

    inode inodo;
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fread(&inodo, sizeof(inode), 1, file);

    string content = read_inode_content(file, super, inodo);

    stringstream ss(content);
    string line;
    int new_index = 1;

    try
    {
        bool existent_grp = false;

        while (getline(ss, line, '\n'))
        {
            stringstream us(line);
            string substr;
            vector<string> words;

            while (getline(us, substr, ','))
            {
                words.push_back(substr);
            }

            if (words.at(0) != "0" && words.at(1) == "G" && words.at(2) == grp)
                existent_grp = true;

            if (words.at(1) == "U")
            {
                new_index += 1;
                if (words.at(3) == usr)
                    throw Exception("already existent user");
            }
        }

        if (!existent_grp)
            throw Exception("non-existent group");

        content += std::to_string(new_index) + ",U," + grp + "," + usr + "," + pwd + "\n";

        file = fopen(disk_path, "r+b");
        write_inode_content(file, &super, &inodo, content);
    }
    catch (const Exception &e)
    {
        strncpy(inodo.atime, date, 16);
        fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
        fwrite(&inodo, sizeof(inode), 1, file);

        strncpy(super.umtime, date, 16);
        fseek(file, partition->start, SEEK_SET);
        fwrite(&super, sizeof(superblock), 1, file);

        throw Exception(e.what());
    }

    strncpy(inodo.mtime, date, 16);
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fwrite(&inodo, sizeof(inode), 1, file);

    strncpy(super.mtime, date, 16);
    fseek(file, partition->start, SEEK_SET);
    fwrite(&super, sizeof(superblock), 1, file);

    fclose(file);

    return 0;
}

int rmusr(string usr)
{
    if (!logged)
        throw Exception("no session active");

    if (user_logged != "root")
        throw Exception("user has no permissions");

    FILE *file = fopen(disk_path, "rb");
    fseek(file, 0, SEEK_SET);

    MBR mbr;
    fread(&mbr, sizeof(MBR), 1, file);

    Partition *partition = getPartition(file, &mbr, partition_name);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char date[17];
    strftime(date, 17, "%d/%m/%Y %H:%M", tm);

    superblock super;
    fseek(file, partition->start, SEEK_SET);
    fread(&super, sizeof(superblock), 1, file);

    inode inodo;
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fread(&inodo, sizeof(inode), 1, file);

    string content = read_inode_content(file, super, inodo);

    stringstream ss(content);
    string line;
    string new_content = "";

    try
    {
        bool found = false;

        while (getline(ss, line, '\n'))
        {
            stringstream us(line);
            string substr;
            vector<string> words;

            while (getline(us, substr, ','))
            {
                words.push_back(substr);
            }

            if (words.at(1) == "U")
            {
                if (words.at(0) != "0" && words.at(3) == usr)
                {
                    found = true;
                    new_content += "0,U," + words.at(2) + "," + words.at(3) + "," + words.at(4) + "\n";
                    continue;
                }
            }

            new_content += line + "\n";
        }

        if (!found)
            throw Exception("non-existent user");

        file = fopen(disk_path, "r+b");
        write_inode_content(file, &super, &inodo, content);
    }
    catch (const Exception &e)
    {
        strncpy(inodo.atime, date, 16);
        fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
        fwrite(&inodo, sizeof(inode), 1, file);

        strncpy(super.umtime, date, 16);
        fseek(file, partition->start, SEEK_SET);
        fwrite(&super, sizeof(superblock), 1, file);

        throw Exception(e.what());
    }

    strncpy(inodo.mtime, date, 16);
    fseek(file, super.inode_start + sizeof(inode), SEEK_SET);
    fwrite(&inodo, sizeof(inode), 1, file);

    strncpy(super.mtime, date, 16);
    fseek(file, partition->start, SEEK_SET);
    fwrite(&super, sizeof(superblock), 1, file);

    fclose(file);

    return 0;
}
