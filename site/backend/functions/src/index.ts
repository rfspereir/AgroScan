import {onCall} from "firebase-functions/v2/https";
import * as logger from "firebase-functions/logger";
import * as admin from "firebase-admin";

admin.initializeApp();

export const createUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado. Apenas root pode criar usuários.");
  }

  const {email, password, nome, clienteid, role} = data;

  if (!email || !password || !nome || !clienteid || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  const user = await admin.auth().createUser({
    email,
    password,
    displayName: nome,
  });

  await admin.auth().setCustomUserClaims(user.uid, {
    clienteid,
    role,
  });

  await admin.database()
    .ref(`clientes/${clienteid}/usuarios/${user.uid}`)
    .set({nome, email, role});

  logger.info(`Usuário criado: ${user.uid}`);

  return {uid: user.uid, message: "Usuário criado com sucesso."};
});
