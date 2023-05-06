-- Créez la base de données
CREATE DATABASE IF NOT EXISTS ventilation_db;
USE ventilation_db;

-- Créez la table Logs
CREATE TABLE IF NOT EXISTS Logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    T INT,
    H INT,
    eCO2 INT,
    Tvoc INT,
    etatVMC INT
);

-- Créez la table Param
CREATE TABLE IF NOT EXISTS Param (
    id INT AUTO_INCREMENT PRIMARY KEY,
    Tmax INT,
    Tmin INT,
    Hmax INT,
    Hmin INT,
    eCO2Max INT,
    eCO2Min INT,
    TvocMax INT,
    TvocMin INT,
    dVentileMax INT,
    dVentileMin INT,
    etatVMC INT
);

-- Créez la table Users
CREATE TABLE IF NOT EXISTS Users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(255),
    password VARCHAR(255)
);

-- Insérez des valeurs par défaut pour les paramètres
INSERT INTO Param (Tmax, Tmin, Hmax, Hmin, eCO2Max, eCO2Min, TvocMax, TvocMin, dVentileMax, dVentileMin, etatVMC)
VALUES (30, 10, 60, 30, 1000, 400, 500, 50, 60, 0, 0);

-- Insérez un utilisateur par défaut
INSERT INTO Users (username, password)
VALUES ('admin', 'password'); -- Remplacez 'password' par un mot de passe plus sûr
