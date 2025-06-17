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

  const {email, password, nome, clienteId, role} = data;

  if (!email || !password || !nome || !clienteId || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  const user = await admin.auth().createUser({
    email,
    password,
    displayName: nome,
  });

  await admin.auth().setCustomUserClaims(user.uid, {
    clienteId,
    role,
  });

  await admin.database()
    .ref(`clientes/${clienteId}/usuarios/${user.uid}`)
    .set({nome, email, role});

  logger.info(`Usuário criado: ${user.uid}`);

  return {uid: user.uid, message: "Usuário criado com sucesso."};
});

export const deleteUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado. Apenas root pode excluir usuários.");
  }

  const {uid, clienteId} = data;

  if (!uid || !clienteId) {
    throw new Error("UID e clienteId são obrigatórios.");
  }

  // 🔥 Remove do Auth
  await admin.auth().deleteUser(uid);

  // 🔥 Remove do RTDB
  await admin.database().ref(`clientes/${clienteId}/usuarios/${uid}`).remove();

  return {message: "Usuário excluído com sucesso."};
});

export const editUser = onCall(async (request) => {
  const auth = request.auth;
  const data = request.data;

  if (auth?.token.role !== "root") {
    throw new Error("Acesso negado.");
  }

  const {uid, clienteId, nome, role} = data;

  if (!uid || !clienteId || !nome || !role) {
    throw new Error("Campos obrigatórios ausentes.");
  }

  // 🔧 Atualiza no RTDB
  await admin.database().ref(`clientes/${clienteId}/usuarios/${uid}`).update({
    nome,
    role,
  });

  // 🔧 Atualiza displayName no Auth (opcional)
  await admin.auth().updateUser(uid, {displayName: nome});

  return {message: "Usuário atualizado com sucesso."};
});

